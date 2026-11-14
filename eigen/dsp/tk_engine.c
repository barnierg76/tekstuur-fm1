/* Tekstuur voor de FM-1: de klankmotor (zie tk_engine.h). Copyright (C) 2026 BARNIR. Alle rechten voorbehouden. */
#include "tk_engine.h"
#include "tk_tables.h"

static const uint16_t TK_HRATIO[TK_HEADS] = {256, 331, 199};
static const int8_t TK_HPAN[TK_HEADS] = {0, -1, 1};
static const int8_t TK_INTERVAL[TK_NINTV][TK_HEADS] = {{0, 0, 0}, {0, 12, -12}, {0, 7, -12}, {0, 12, 7}, {0, -12, -24}};

static inline int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline int32_t sat16(int32_t v) { return clampi(v, -32768, 32767); }
static uint32_t rnd(tk_t *t) { t->rng = t->rng * 1664525u + 1013904223u; return t->rng >> 8; }

/* 2^(x / 192) in Q12; x in 1/16 halve toon, -8..+8 octaven */
static uint32_t pow2_q12(int32_t x)
{
    int32_t o = x >= 0 ? x / 192 : -((-x + 191) / 192), f = x - o * 192;
    uint32_t r = TK_POW2[f];           /* Q15 */
    o -= 3;                            /* naar Q12 */
    return o >= 0 ? r << o : r >> -o;
}
/* base * 2^(v * k / 192), voor de bereiken van de regelaars */
/* a * b >> 12 over 64 bits. Niet inline: op de FM-1 gaat een 64-bitsvermenigvuldiging tussen 16-bitsleesacties soms
 * fout (felucca-bsp docs/CPU.md), dus staat ze apart en nooit in een lus per sample */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
static uint32_t mul_q12(uint32_t a, uint32_t b) { return (uint32_t)(((uint64_t)a * b) >> 12); }
static uint32_t expo(uint32_t base, uint32_t v, uint32_t k) { return mul_q12(base, pow2_q12((int32_t)(v * k))); }

/* een sample van de bron, 16-bitsschaal */
static inline int32_t rd(const uint8_t *d, uint32_t i)
{
    const uint8_t *b = d + (i >> 1) * 3u;
    int32_t v = i & 1u ? (b[1] >> 4) | (b[2] << 4) : b[0] | ((b[1] & 15) << 8);
    return ((v ^ 0x800) - 0x800) << 4;
}

/* het venster van een doorgang op leeftijd age, Q15: sin^2 omhoog over ramp, vlak, sin^2 omlaag */
static int32_t win(uint32_t age, uint32_t dur, uint32_t ramp)
{
    uint32_t a;
    if (age >= dur || !ramp)
        return 0;
    a = age < ramp ? age : dur - age < ramp ? dur - age : ramp;
    if (a >= ramp)
        return 32767;
    return TK_SIN2[(a << 8) / ramp];
}
static int32_t tri(uint32_t ph) { int32_t x = (int32_t)(ph >> 16); return x < 32768 ? x - 16384 : 49152 - x; }   /* -16384..16384 */

uint32_t tk_passes(const tk_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < TK_NP; i++)
        n += t->ps[i].on;
    return n;
}

void tk_init(tk_t *t, const tk_src_t *src, uint32_t nsrc, int16_t *dly_mem, int16_t *rv_mem, int32_t *res_mem)
{
    uint32_t i;
    uint8_t *b = (uint8_t *)t;
    for (i = 0; i < sizeof *t; i++)
        b[i] = 0;
    t->src = src;
    t->nsrc = nsrc;
    t->dly_l = dly_mem;
    t->dly_r = dly_mem + TK_DLY_MAX;
    t->rv = rv_mem;
    t->shm = rv_mem + 8192;
    for (i = 0; i < TK_NRES; i++)
        t->ru[i].d = res_mem + i * TK_HEADS * TK_RES_LEN;
    for (i = 0; i < TK_NRES * TK_HEADS * TK_RES_LEN; i++)
        res_mem[i] = 0;
    for (i = 0; i < 2u * TK_DLY_MAX; i++)
        dly_mem[i] = 0;
    for (i = 0; i < TK_RV_MEM; i++)
        rv_mem[i] = 0;
    t->rng = 0x1234567u;
    t->np_max = TK_NP;
    t->e2 = 0x40000000u;
    for (i = 0; i < TK_NL; i++) {
        t->cutq[i] = 4096;
        t->resq[i] = 8192;
        t->openq[i] = 4096;
    }
}

static tk_pass_t *pass_new(tk_t *t)
{
    uint32_t i;
    tk_pass_t *q = 0;
    uint32_t used = 0;
    for (i = 0; i < TK_NP; i++)
        used += t->ps[i].on;
    if (used < t->np_max)
        for (i = 0; i < TK_NP; i++)
            if (!t->ps[i].on)
                return &t->ps[i];
    for (i = 0; i < TK_NP; i++)        /* vol: de zachtste doorgang dooft dit blok uit, de nieuwe wacht een blok */
        if (!t->ps[i].kill && (!q || t->ps[i].g0 < q->g0))
            q = &t->ps[i];
    if (q)
        q->kill = 1;
    return 0;
}

/* een doorgang van laag li voor noot v: kop / korrel k. dur in uitvoersamples (wordt ingekort als de bron te kort is),
 * posv 0..127 << 8 de plek, semis16 de toonhoogte, cents de zweving, pan -127..127, gain Q15. -> dur, 0 = geen plek */
static uint32_t spawn(tk_t *t, uint32_t vi, uint32_t li, uint32_t dur, uint32_t ramp_div, int32_t posv, int32_t semis16,
                      int32_t det, int32_t pan, int32_t gain, uint32_t late)
{
    const tk_layer_t *L = &t->p.l[li];
    const tk_src_t *s = &t->src[L->src % t->nsrc];
    tk_voice_t *v = &t->v[vi];
    tk_pass_t *p;
    uint32_t rate, span, room;
    rate = mul_q12(pow2_q12(semis16), (s->rate << 12) / TK_FS);
    rate = (uint32_t)((int32_t)rate + (((int32_t)rate >> 6) * det >> 9));          /* det 127 = 1,5 % */
    if (rate < 64u) rate = 64u;
    if (mul_q12(dur, rate) > s->len - 2u)
        dur = ((s->len - 2u) << 12) / rate;                /* (bronnen korter dan 2^20 samples) */
    if (dur < 2u * TK_BLK)
        return 2u * TK_BLK;
    span = mul_q12(dur, rate);
    room = s->len - 2u - span;
    p = pass_new(t);
    if (!p)
        return 0;
    p->on = 1; p->kill = 0;
    p->layer = (uint8_t)li; p->voice = (uint8_t)vi;
    p->s = s;
    p->rate = rate;
    p->pos = ((room * (uint32_t)clampi(posv, 0, 127 << 8) / (127u << 8)) << 12) + late * rate;
    p->age = late;
    p->dur = dur;
    p->ramp = dur / ramp_div;
    pan = clampi(pan, -127, 127);
    gain = gain * v->vel / 127;
    p->gl = pan > 0 ? gain * (127 - pan) / 127 : gain;
    p->gr = pan < 0 ? gain * (127 + pan) / 127 : gain;
    p->g0 = win(p->age, p->dur, p->ramp) * v->env[li] >> 15;
    return dur;
}

static uint32_t isqrt(uint32_t x)
{
    uint32_t r = 0, b = 1u << 30;
    while (b > x) b >>= 2;
    while (b) {
        if (x >= r + b) { x -= r + b; r = (r >> 1) + b; } else r >>= 1;
        b >>= 2;
    }
    return r;
}

/* de periode van een toon, Q8 samples: semis16 in 1/16 halve toon boven MIDI-noot 69 */
static uint32_t period_q8(int32_t semis16)
{
    uint32_t p = mul_q12(12829u, pow2_q12(-semis16));      /* 22050 / 440 * 256 */
    while (p > ((TK_RES_LEN - 4u) << 8))                   /* te laag voor de snaar: een octaaf hoger */
        p >>= 1;
    return p < (4u << 8) ? 4u << 8 : p;
}

/* snaren voor noot vi op laag li: een vrij stel, anders het stilste stel van een noot die al los is -> 255 = geen */
static uint32_t res_alloc(tk_t *t, uint32_t vi, uint32_t li)
{
    uint32_t i, best = 255, k;
    for (i = 0; i < TK_NRES; i++) {
        tk_res_t *r = &t->ru[i];
        int owned = t->v[r->voice].on && t->v[r->voice].ru[r->layer] == i;
        if (!r->on) { best = i; break; }
        if (!owned && (best == 255 || r->peak < t->ru[best].peak))
            best = i;
    }
    if (best != 255) {
        tk_res_t *r = &t->ru[best];
        for (k = 0; k < TK_HEADS * TK_RES_LEN; k++)
            r->d[k] = 0;
        r->on = 1; r->layer = (uint8_t)li; r->voice = (uint8_t)vi; r->wi = 0; r->peak = 0;
        r->lp[0] = r->lp[1] = r->lp[2] = 0;
        r->dc[0] = r->dc[1] = r->dc[2] = 0;
    }
    return best;
}

static void voice_block(tk_t *t, uint32_t vi)
{
    tk_voice_t *v = &t->v[vi];
    uint32_t li, h, alive = v->held;
    for (li = 0; li < TK_NL; li++) {
        const tk_layer_t *L = &t->p.l[li];
        const tk_src_t *s = &t->src[L->src % t->nsrc];
        int32_t base = ((int32_t)v->note - s->root + L->ptch) * 16;
        int32_t ev = (li & 1u ? tri(t->e2) : tri(t->e1)) * (int32_t)t->p.evo / 127 * L->evo / 127;   /* -16384..16384 */
        int32_t posv = ((int32_t)L->pos << 8) + (ev >> 1) * ((li & 2u) ? -1 : 1);
        int32_t step;
        if (L->eng == TK_OFF) { v->env[li] = 0; continue; }
        /* de omhullende van deze noot op deze laag */
        if (v->held) {
            step = (int32_t)((32767u * TK_BLK) / expo(110, L->atk, 15)) + 1;
            v->env[li] = v->env[li] + step > 32767 ? 32767 : v->env[li] + step;
        } else {
            step = (int32_t)((32767u * TK_BLK) / expo(441, L->rel, 13)) + 1;
            v->env[li] = v->env[li] - step < 0 ? 0 : v->env[li] - step;
        }
        alive |= v->env[li] > 0;
        if (!v->env[li] && !v->held)
            continue;
        if (L->eng == TK_RESON) {                  /* de snaren van deze noot: gestemd, en hoe lang ze naklinken */
            uint32_t t60 = expo(4410, L->len, 9);              /* 0,2 .. 12 s */
            tk_res_t *r;
            if (v->ru[li] == 255 && v->held)
                v->ru[li] = (uint8_t)res_alloc(t, vi, li);
            if (v->ru[li] != 255) {
                r = &t->ru[v->ru[li]];
                for (h = 0; h < TK_HEADS; h++) {
                    int32_t semis = ((int32_t)v->note - 69 + L->ptch + TK_INTERVAL[L->intv % TK_NINTV][h]) * 16
                                    + (TK_HPAN[h] * L->detune >> 3);
                    uint32_t loss;
                    int32_t pan = TK_HPAN[h] * L->width;
                    r->per[h] = period_q8(semis);
                    loss = 226361u / 256u * (r->per[h] >> 8) / (t60 >> 8) * 1u;     /* 6,9 x periode / naklinktijd, Q15 */
                    if (loss < 6u) loss = 6u;
                    if (loss > 16000u) loss = 16000u;
                    r->fb[h] = 32768 - (int32_t)loss;
                    r->ing[h] = (int32_t)isqrt(loss << 16);    /* wortel(2 x verlies): het niveau hangt niet van de naklinktijd af */
                    r->gl[h] = pan > 0 ? 32767 * (127 - pan) / 127 : 32767;
                    r->gr[h] = pan < 0 ? 32767 * (127 + pan) / 127 : 32767;
                }
            }
        }
        if (L->eng == TK_LOOP) {
            uint32_t dur0 = expo(3308, L->len, 8);
            for (h = 0; h < TK_HEADS; h++) {
                v->cnt[li][h] -= TK_BLK;
                if (v->cnt[li][h] <= 0) {
                    uint32_t dur = dur0 * TK_HRATIO[h] >> 8, late = (uint32_t)(-v->cnt[li][h]) % TK_BLK, d;
                    d = spawn(t, vi, li, dur, 4, posv + TK_HPAN[h] * L->sprd * 40, base + TK_INTERVAL[L->intv % TK_NINTV][h] * 16,
                              TK_HPAN[h] * L->detune, TK_HPAN[h] * L->width, 18900, late);
                    if (d)
                        v->cnt[li][h] += (int32_t)(d - d / 4u);
                    else
                        v->cnt[li][h] = 0;     /* geen plek: volgend blok opnieuw */
                }
            }
        } else if (L->eng == TK_STRETCH) {     /* dichte korrels op een plek die langzaam door de bron schuift */
            uint32_t dur = expo(1323, L->len, 6);
            if (L->dens)
                v->scan[li] += expo(151, L->dens, 10);         /* heen en weer: 80 s .. 1 s */
            v->cnt[li][0] -= TK_BLK;
            if (v->cnt[li][0] <= 0) {
                uint32_t r = rnd(t), r2 = rnd(t), d;
                int32_t sc = (int32_t)(((uint32_t)posv + (v->scan[li] >> 8)) % 65024u);
                if (posv < 0) sc = 0;
                if (sc > 32512) sc = 65024 - sc;
                d = spawn(t, vi, li, dur, 2, sc + (((int32_t)(r & 0xFFFFu) - 32768) * L->sprd >> 10),
                          base + TK_INTERVAL[L->intv % TK_NINTV][r2 % TK_HEADS] * 16, ((int32_t)((r2 >> 8) & 0xFFu) - 128) * L->detune >> 7,
                          ((int32_t)((r >> 16) & 0xFFu) - 128) * L->width >> 7, 30000, (uint32_t)(-v->cnt[li][0]) % TK_BLK);
                v->cnt[li][0] = d ? v->cnt[li][0] + (int32_t)(d / 4u) : 0;
            }
        } else {                               /* TK_GRAIN, en de aanslag van TK_RESON */
            uint32_t dur = L->eng == TK_RESON ? 1764u : expo(441, L->len, 7), gps = expo(512, L->dens, 8);   /* korrels per seconde, Q8 */
            uint32_t over = gps * dur / TK_FS;                                  /* overlap, Q8 */
            int32_t gain = over > 256u ? (int32_t)(26000u * 16u / isqrt(over)) : 26000;
            v->acc[li] += gps * TK_BLK;
            while (v->acc[li] >= (uint32_t)TK_FS << 8) {
                uint32_t r = rnd(t);
                int32_t jit = (int32_t)(r & 0xFFFFu) - 32768;                   /* plek */
                int32_t pj = (int32_t)((r >> 16) & 0xFFu) - 128;                /* panorama */
                uint32_t r2 = rnd(t);
                v->acc[li] -= (uint32_t)TK_FS << 8;
                spawn(t, vi, li, dur, 2, posv + (jit * L->sprd >> 8), base + TK_INTERVAL[L->intv % TK_NINTV][r2 % TK_HEADS] * 16,
                      ((int32_t)((r2 >> 8) & 0xFFu) - 128) * L->detune >> 7, pj * L->width >> 7, gain, (r2 >> 16) % TK_BLK);
            }
        }
    }
    if (!alive)
        v->on = 0;
}

void tk_note_on(tk_t *t, uint32_t note, uint32_t vel)
{
    uint32_t i, li, h, best = 0, score = 0;
    tk_voice_t *v;
    for (i = 0; i < TK_NV; i++) {
        uint32_t s = !t->v[i].on ? 0xFFFFFFFFu : (t->v[i].held ? 0u : 0x80000000u) + (t->v[i].age >> 1);
        if (t->v[i].on && t->v[i].note == note) { best = i; break; }
        if (s >= score) { score = s; best = i; }
    }
    v = &t->v[best];
    if (v->on)
        for (i = 0; i < TK_NP; i++)
            if (t->ps[i].on && t->ps[i].voice == best)
                t->ps[i].kill = 1;
    v->on = 1; v->held = 1; v->note = (uint8_t)note; v->vel = (uint8_t)(vel ? vel : 1); v->age = 0;
    for (li = 0; li < TK_NL; li++) {
        v->env[li] = 0;
        v->acc[li] = (uint32_t)TK_FS << 8;         /* de eerste korrel meteen */
        v->scan[li] = 0;
        v->ru[li] = 255;
        for (h = 0; h < TK_HEADS; h++)
            v->cnt[li][h] = TK_BLK;
    }
}
void tk_note_off(tk_t *t, uint32_t note)
{
    uint32_t i;
    for (i = 0; i < TK_NV; i++)
        if (t->v[i].on && t->v[i].held && t->v[i].note == note)
            t->v[i].held = 0;
}
void tk_all_off(tk_t *t)
{
    uint32_t i;
    for (i = 0; i < TK_NV; i++)
        t->v[i].held = 0;
}

/* galm: twee allpass-filters, dan vier vertragingslijnen met een Hadamard-terugkoppeling */
#define RV_A0 0u
#define RV_A0N 107u
#define RV_A1 107u
#define RV_A1N 167u
#define RV_A2 274u
#define RV_A2N 229u
static const uint16_t RV_L[4] = {1103, 1361, 1597, 1811};
static const uint16_t RV_O[4] = {512, 1640, 3040, 4660};

static int32_t softclip(int32_t x)
{
    int32_t a = x < 0 ? -x : x, e;
    if (a <= 24000)
        return x;
    e = a - 24000;
    if (e > 240000) e = 240000;        /* (het product past dan in 32 bits) */
    e = e * 8767 / (e + 8767);
    return x < 0 ? -(24000 + e) : 24000 + e;
}

void tk_render(tk_t *t, int32_t *lr, uint32_t n)
{
    static int32_t bus[TK_NL][2 * TK_BLK], mix[2 * TK_BLK], srev[TK_BLK], sdly[TK_BLK], exc[TK_NRES][TK_BLK];
    const tk_patch_t *P = &t->p;
    uint32_t done, i, li;
    for (done = 0; done < n; done += TK_BLK, lr += 2 * TK_BLK) {
        /* Evolve: twee trage driehoeken met een onderling ondeelbare duur */
        uint32_t tblk = expo(345, P->evo_time, 9);                 /* blokken per reis: 5 s .. 5 min */
        int32_t vx, vy, w[4];
        t->e1 += 0xFFFFFFFFu / tblk;
        t->e2 += 0xFFFFFFFFu / (tblk * 37u / 23u);
        vx = clampi(P->vx + (tri(t->e2) * (int32_t)P->evo >> 16), 0, 127);
        vy = clampi(P->vy + (tri(t->e1) * (int32_t)P->evo >> 16), 0, 127);
        t->vx_now = (uint8_t)vx; t->vy_now = (uint8_t)vy;
        w[0] = clampi(2 * (127 - vx), 0, 127); w[1] = clampi(2 * vx, 0, 127);
        w[2] = clampi(2 * (127 - vy), 0, 127); w[3] = clampi(2 * vy, 0, 127);
        for (li = 0; li < TK_NL; li++) {
            int32_t target = w[li & 1u] * w[2 + (li >> 1)] * 2;    /* Q15 */
            int32_t ev = (li & 1u ? tri(t->e1) : tri(t->e2)) * (int32_t)P->evo / 127 * P->l[li].evo / 127;
            int32_t cut = clampi(P->l[li].cut + (ev >> 9), 0, 127), cq;
            t->vg[li] += (target - t->vg[li]) >> 4;
            /* het filter rekent twee keer per sample: f = 2 pi fc / 44100, Q12, tot 1 (7 kHz); daarboven gaat het
             * geleidelijk helemaal open (cut 104 .. 127) */
            cq = (int32_t)(expo(80, (uint32_t)cut, 12) * 584u / 1000u);
            t->cutq[li] = clampi(cq, 24, 4096);
            t->resq[li] = 8192 - P->l[li].res * 56;            /* demping 2 .. 0,26 */
            {                                                  /* stabiel zolang demping < 2 / f - f / 2: daar 10 % onder */
                int32_t qmax = (8192 * 4096 / t->cutq[li] - t->cutq[li] / 2) * 9 / 10;
                if (t->resq[li] > qmax) t->resq[li] = qmax;
            }
            t->openq[li] = clampi((cut - 104) * 178, 0, 4096);
            for (i = 0; i < 2u * TK_BLK; i++)
                bus[li][i] = 0;
        }
        for (li = 0; li < TK_NRES; li++)
            if (t->ru[li].on)
                for (i = 0; i < TK_BLK; i++)
                    exc[li][i] = 0;
        for (i = 0; i < TK_NV; i++)
            if (t->v[i].on) {
                voice_block(t, i);
                t->v[i].age += TK_BLK;
            }
        /* de doorgangen */
        for (i = 0; i < TK_NP; i++) {
            tk_pass_t *p = &t->ps[i];
            const uint8_t *d;
            uint32_t pos, rate, last, k;
            int32_t g1, gl, gr, dl, dr, *o;
            if (!p->on)
                continue;
            p->age += TK_BLK;
            g1 = p->kill || !t->v[p->voice].on ? 0 : win(p->age, p->dur, p->ramp) * t->v[p->voice].env[p->layer] >> 15;
            gl = (p->g0 * p->gl >> 15) << 8; gr = (p->g0 * p->gr >> 15) << 8;
            dl = ((g1 * p->gl >> 15) * 256 - gl) / TK_BLK; dr = ((g1 * p->gr >> 15) * 256 - gr) / TK_BLK;
            d = p->s->d; pos = p->pos; rate = p->rate; last = p->s->len - 2u;
            o = bus[p->layer];
            if (P->l[p->layer].eng == TK_RESON) {            /* naar de snaren van zijn noot, mono */
                uint32_t u = t->v[p->voice].on ? t->v[p->voice].ru[p->layer] : 255u;
                int32_t g = (gl + gr) >> 1, dg = (dl + dr) / 2;
                if (u != 255u)
                    for (k = 0; k < TK_BLK; k++) {
                        uint32_t ix = pos >> 12;
                        int32_t a, b;
                        if (ix > last) ix = last;
                        a = rd(d, ix); b = rd(d, ix + 1u);
                        g += dg;
                        exc[u][k] += (a + ((b - a) * (int32_t)(pos & 4095u) >> 12)) * (g >> 8) >> 15;
                        pos += rate;
                    }
                else
                    pos += rate * TK_BLK;
            } else
            for (k = 0; k < TK_BLK; k++) {
                uint32_t ix = pos >> 12;
                int32_t a, b, s;
                if (ix > last) ix = last;
                a = rd(d, ix); b = rd(d, ix + 1u);
                s = a + ((b - a) * (int32_t)(pos & 4095u) >> 12);
                gl += dl; gr += dr;
                o[2u * k] += s * (gl >> 8) >> 15;
                o[2u * k + 1u] += s * (gr >> 8) >> 15;
                pos += rate;
            }
            p->pos = pos;
            p->g0 = g1;
            if (p->kill || p->age >= p->dur || (!g1 && !t->v[p->voice].on))
                p->on = 0;
        }
        if (tk_passes(t) > t->npass_peak) t->npass_peak = tk_passes(t);
        /* de snaren: elk een vertraging van een periode met terugkoppeling, aangeslagen door de korrels van hun noot */
        for (i = 0; i < TK_NRES; i++) {
            tk_res_t *r = &t->ru[i];
            uint32_t h, k, pk = 0, wi;
            int32_t *o;
            if (!r->on)
                continue;
            o = bus[r->layer];
            for (h = 0; h < TK_HEADS; h++) {
                int32_t *d = r->d + h * TK_RES_LEN;
                uint32_t di = (r->per[h] - 94u) >> 8, fr = (r->per[h] - 94u) & 255u;   /* (het filter in de lus vertraagt 0,37 sample) */
                int32_t fb = r->fb[h], ing = r->ing[h], lp = r->lp[h], dc = r->dc[h], gl = r->gl[h], gr = r->gr[h];
                wi = r->wi;
                for (k = 0; k < TK_BLK; k++) {
                    int32_t a = d[(wi - di) & (TK_RES_LEN - 1u)], b = d[(wi - di - 1u) & (TK_RES_LEN - 1u)], y;
                    a += ((b - a) >> 4) * (int32_t)fr >> 4;
                    lp += (a - lp) * 3 / 4;
                    /* alles 256 x de waarde, en alleen het verlies wordt afgerond: de staart dooft glad uit */
                    y = (clampi(exc[i][k], -32767, 32767) * ing >> 7) + lp - ((lp >> 8) * (32768 - fb) >> 7);
                    y -= dc >> 8;                              /* geen gelijkspanning in de lus: een kamfilter versterkt die ook */
                    dc += y;
                    y = clampi(y, -8388607, 8388607);
                    d[wi & (TK_RES_LEN - 1u)] = y;
                    if ((uint32_t)(y < 0 ? -y : y) > pk) pk = (uint32_t)(y < 0 ? -y : y);
                    o[2u * k] += (y >> 8) * gl >> 15;
                    o[2u * k + 1u] += (y >> 8) * gr >> 15;
                    wi++;
                }
                r->lp[h] = lp;
                r->dc[h] = dc;
            }
            r->wi += TK_BLK;
            r->peak = pk;
            if (pk < 256u && !(t->v[r->voice].on && t->v[r->voice].ru[r->layer] == i))
                r->on = 0;                                     /* uitgeklonken en van niemand meer */
        }
        /* per laag: filter, niveau, vector; naar de mix en de sends */
        for (i = 0; i < 2u * TK_BLK; i++) mix[i] = 0;
        for (i = 0; i < TK_BLK; i++) srev[i] = sdly[i] = 0;
        for (li = 0; li < TK_NL; li++) {
            const tk_layer_t *L = &P->l[li];
            int32_t g = (t->vg[li] >> 3) * L->level / 127, f = t->cutq[li], q = t->resq[li], op = t->openq[li], *z = t->fl[li];   /* g Q12 */
            g = g * (360 - L->res) / 360;                          /* resonantie maakt luider: iets terug */
            if (L->eng == TK_OFF)
                continue;
            for (i = 0; i < TK_BLK; i++) {
                int32_t l = clampi(bus[li][2u * i], -131071, 131071) * 16, r = clampi(bus[li][2u * i + 1u], -131071, 131071) * 16;
                /* toestandsfilter (laag, band), twee keer per sample; de toestanden 16 x de waarde, begrensd */
#define SVF(lo, bd, in) (z[lo] += (z[bd] / 16) * f >> 8, z[bd] += (((in) - z[lo] - ((z[bd] / 16) * q >> 8)) / 16) * f >> 8, \
                         z[lo] = clampi(z[lo], -4194303, 4194303), z[bd] = clampi(z[bd], -4194303, 4194303))
                SVF(0, 1, l); SVF(0, 1, l); SVF(2, 3, r); SVF(2, 3, r);
                l = (z[0] + ((l - z[0]) / 4096) * op) / 16; r = (z[2] + ((r - z[2]) / 4096) * op) / 16;
                l = l * g >> 12; r = r * g >> 12;
                mix[2u * i] += l; mix[2u * i + 1u] += r;
                srev[i] += (l + r) * L->rev >> 8;
                sdly[i] += (l + r) * L->dly >> 8;
            }
        }
        /* effecten en uitgang */
        {
            uint32_t dn = expo(1323, P->dly_time, 5);              /* 60 ms .. 0,6 s */
            int32_t fb = P->dly_fb * 230, rg = 20000 + P->rev_size * 98, damp = 32767 - P->rev_damp * 150, shg, frz, thaw;
            int16_t *sh = t->shm;
            /* bevriezen: niets meer erin, niets meer eruit weglekken; glijdend in ongeveer 0,1 s */
            t->frz += ((t->freeze ? 32768 : 0) - t->frz) / 32 + (t->freeze ? 1 : 0);
            if (t->frz > 32768) t->frz = 32768;
            if (!t->freeze && t->frz < 32) t->frz = 0;
            frz = t->frz; thaw = 32768 - frz;
            rg += (32768 - rg) * (frz >> 3) / 4096;
            damp += (32767 - damp) * (frz >> 3) / 4096;
            /* shimmer: hoe groter de galm, hoe minder er terug mag, anders loopt het op */
            shg = (int32_t)isqrt((uint32_t)((32768 - rg) * ((32768 + rg) >> 4)) << 4) * P->shim / 127 * 3 / 8 * (thaw >> 3) / 4096;
            int32_t cho = P->cho * 200, master = P->master * 560;      /* 127 = ruim twee keer: zachte klanken kunnen omhoog */
            int16_t *rv = t->rv;
            if (dn > TK_DLY_MAX - 1u) dn = TK_DLY_MAX - 1u;
            for (i = 0; i < TK_BLK; i++) {
                int32_t l = mix[2u * i], r = mix[2u * i + 1u], x, y, q[4], s0, s1, s2, s3, wl, wr;
                uint32_t wi = t->wi & 1023u, ph, di, k;
                /* chorus: twee zwevende aftakkingen, tegengesteld voor links en rechts */
                t->cho_l[wi] = (int16_t)sat16(l); t->cho_r[wi] = (int16_t)sat16(r);
                t->lfo += 68174u;
                ph = t->lfo >> 24;
                x = (265 << 8) + TK_SINE[ph] * 85 / 128;          /* vertraging in samples, Q8 */
                y = (265 << 8) - TK_SINE[(ph + 64u) & 255u] * 85 / 128;
                di = (wi - (uint32_t)(x >> 8)) & 1023u;
                wl = t->cho_l[di] + ((t->cho_l[(di - 1u) & 1023u] - t->cho_l[di]) * (x & 255) >> 8);
                di = (wi - (uint32_t)(y >> 8)) & 1023u;
                wr = t->cho_r[di] + ((t->cho_r[(di - 1u) & 1023u] - t->cho_r[di]) * (y & 255) >> 8);
                l += wr * cho >> 15; r += wl * cho >> 15;          /* gekruist: breder */
                t->wi++;
                /* delay: heen en weer */
                di = t->dwi >= dn ? t->dwi - dn : t->dwi + TK_DLY_MAX - dn;
                x = t->dly_l[di]; y = t->dly_r[di];
                t->dlp_l += (x - t->dlp_l) / 2; t->dlp_r += (y - t->dlp_r) / 2;
                t->dly_l[t->dwi] = (int16_t)sat16(sdly[i] + t->dlp_r * fb / 32768);
                t->dly_r[t->dwi] = (int16_t)sat16(t->dlp_l * fb / 32768);
                if (++t->dwi >= TK_DLY_MAX) t->dwi = 0;
                l += x; r += y;
                /* galm */
                x = (srev[i] + (x + y) / 4) / 4 * (thaw >> 3) / 4096;
                {                                                  /* shimmer: twee aftakkingen die dubbel zo snel lezen */
                    uint32_t d1 = (TK_SHM - 1u) - (t->shw & (TK_SHM - 1u)), d2 = (d1 + TK_SHM / 2u) & (TK_SHM - 1u);
                    int32_t w1 = (int32_t)(d1 < TK_SHM / 2u ? d1 : TK_SHM - d1), w2 = (int32_t)(TK_SHM / 2u) - w1;
                    int32_t up = (sh[(t->shw - d1) & (TK_SHM - 1u)] * w1 + sh[(t->shw - d2) & (TK_SHM - 1u)] * w2) / (int32_t)(TK_SHM / 2u);
                    x += up * shg / 32768;
                }
                for (k = 0; k < 3u; k++) {                         /* allpass */
                    uint32_t base = k == 0 ? RV_A0 : k == 1 ? RV_A1 : RV_A2, len = k == 0 ? RV_A0N : k == 1 ? RV_A1N : RV_A2N;
                    uint32_t ix = base + t->ri[k];
                    if (++t->ri[k] >= len) t->ri[k] = 0;
                    int32_t dly = rv[ix], in = x + dly / 2;
                    rv[ix] = (int16_t)sat16(in);
                    x = dly - in / 2;
                }
                for (k = 0; k < 4u; k++) {
                    q[k] = rv[RV_O[k] + t->ri[3u + k]];
                    t->rlp[k] += (q[k] - t->rlp[k]) * damp / 32768;
                    q[k] = t->rlp[k];
                }
                s0 = (q[0] + q[1] + q[2] + q[3]) / 2; s1 = (q[0] - q[1] + q[2] - q[3]) / 2;
                s2 = (q[0] + q[1] - q[2] - q[3]) / 2; s3 = (q[0] - q[1] - q[2] + q[3]) / 2;
                rv[RV_O[0] + t->ri[3]] = (int16_t)sat16(x + s0 * rg / 32768);
                rv[RV_O[1] + t->ri[4]] = (int16_t)sat16(x + s1 * rg / 32768);
                rv[RV_O[2] + t->ri[5]] = (int16_t)sat16(x + s2 * rg / 32768);
                rv[RV_O[3] + t->ri[6]] = (int16_t)sat16(x + s3 * rg / 32768);
                for (k = 0; k < 4u; k++)
                    if (++t->ri[3u + k] >= RV_L[k]) t->ri[3u + k] = 0;
                t->shlp[0] += ((q[0] + q[1] + q[2] + q[3]) / 2 - t->shlp[0]) / 2;   /* wat omhoog gaat eerst doffer: minder vouwen */
                t->shlp[1] += (t->shlp[0] - t->shlp[1]) / 2;
                {                                                  /* en zonder laag: daar telt de galm alles bij elkaar op */
                    int32_t v = t->shlp[1] - (t->shdc >> 5);
                    t->shdc += v;
                    sh[t->shw & (TK_SHM - 1u)] = (int16_t)sat16(v);
                }
                t->shw++;
                l += (q[0] + q[2]) * 2; r += (q[1] + q[3]) * 2;
                l -= t->dc_l >> 10; t->dc_l += l;                  /* gelijkspanning eruit (3 Hz) */
                r -= t->dc_r >> 10; t->dc_r += r;
                l = softclip(l * master >> 15); r = softclip(r * master >> 15);
                lr[2u * i] = l << 8; lr[2u * i + 1u] = r << 8;
            }
        }
    }
}

/* de grootste uitslag van bron s tussen sample a en b (voor de golfvorm op het scherm), 0..2047 */
uint32_t tk_src_peak(const tk_src_t *s, uint32_t a, uint32_t b)
{
    uint32_t i, pk = 0;
    for (i = a; i < b && i < s->len; i++) {
        int32_t v = rd(s->d, i) >> 4;
        if (v < 0) v = -v;
        if ((uint32_t)v > pk) pk = (uint32_t)v;
    }
    return pk;
}
