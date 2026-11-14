#include "tx_engine.h"

#include "tx_tables.h"

/* 2^(x / (12 * 256)) in Q16 for x in semitones * 256 */
static uint32_t tx_ratio(int32_t semi16)
{
    int32_t oct, f;
    uint32_t r;
    int64_t t = (int64_t)semi16 * 256 / 3072;         /* 1/256 octaves */
    oct = (int32_t)(t >> 8);
    f = (int32_t)(t & 255);
    r = TX_POW[f];
    return oct >= 0 ? r << oct : r >> -oct;
}

static uint32_t tx_rnd(tx_part_t *P)
{
    P->rng = P->rng * 1664525u + 1013904223u;
    return P->rng >> 8;
}
static int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

/* the loop lengths of the heads against LEN, Q8: primes, so the pattern of the three repeats
 * only after 256 * 331 * 199 passes */
static const uint16_t TX_HRATIO[TX_HEADS] = {256, 331, 199};
/* where a head reads against POS, in 1/256 of the sample */
static const int16_t TX_HOFFS[TX_HEADS] = {0, 29, -19};
/* the interval of each head, semitones */
static const int8_t TX_INTERVAL[TX_NINTV][TX_HEADS] = {
    {0, 0, 0},       /* UNI: one pitch, a chorus through DRFT */
    {0, 12, -12},    /* OCT */
    {0, 7, -12},     /* 5TH */
    {0, 12, 7},      /* OCT5 */
    {0, -12, -24},   /* SUB */
};

void tx_init(tx_part_t *P, const int16_t *data, uint32_t n, int32_t root_note)
{
    uint32_t i;
    uint8_t *b = (uint8_t *)P;
    for (i = 0; i < sizeof *P; i++)
        b[i] = 0;
    P->zone.data = data;
    P->zone.n = n;
    P->zone.root16 = root_note << 8;
    P->rng = 0x2545F491u;
    P->par[TX_POS] = 40; P->par[TX_LEN] = 80; P->par[TX_INTV] = TX_UNI; P->par[TX_PTCH] = 0;
    P->par[TX_DRFT] = 30; P->par[TX_EVOL] = 0; P->par[TX_TONE] = 127;
}

/* EVOL: two slow triangles (23 s and 37 s) summed, -32768..32767 */
static int32_t tx_tri(uint32_t ph) { uint32_t t = ph >> 15; return (int32_t)(t < 65536u ? t : 131071u - t) - 32768; }
static int32_t tx_evolve(const tx_part_t *P) { return (tx_tri(P->evo1) * 3 + tx_tri(P->evo2) * 2) / 5; }

uint32_t tx_position(const tx_part_t *P)
{
    int32_t c = (P->par[TX_POS] * 65535) / 127 + ((tx_evolve(P) * P->par[TX_EVOL]) / 127);
    return (uint32_t)clampi(c, 0, 65535);
}

static uint32_t tx_heads_for(const tx_part_t *P)
{
    uint32_t i, nact = 0, cap;
    for (i = 0; i < TX_POLY; i++)
        nact += P->v[i].active;
    cap = TX_NP / (nact ? nact : 1u) / 2u;            /* a head holds two passes while it fades */
    return cap > TX_HEADS ? TX_HEADS : cap < 1u ? 1u : cap;
}

/* start a pass of head h of voice vi; returns its period (output samples to the next start) */
/* `late`: output samples the start is behind its time (starts happen on block borders): the pass
 * begins that far into its window and its stretch, so it meets the fade of the pass before it exactly */
static uint32_t tx_spawn(tx_part_t *P, uint32_t vi, uint32_t h, uint32_t heads, uint32_t late)
{
    const tx_zone_t *z = &P->zone;
    tx_voice_t *v = &P->v[vi];
    tx_pass_t *g = 0;
    uint32_t i, len, step, span, fade, r1 = tx_rnd(P), r2 = tx_rnd(P), n = z->n;
    int32_t d16, cents, pos, c, jit;
    /* LEN: 50 ms * 2^(LEN / 20): 50 ms .. 4 s, times the head's ratio */
    len = (uint32_t)(((uint64_t)(TX_RATE / 20) * tx_ratio(P->par[TX_LEN] * 3072 / 20)) >> 16);
    len = (len * TX_HRATIO[h]) >> 8;
    /* rate: the note against the sample's root, PTCH, the head's interval, DRFT^2 up to +-50 cents */
    cents = P->par[TX_DRFT] * P->par[TX_DRFT] * 128 / (127 * 127);       /* 1/256 semitones */
    d16 = v->pitch16 - z->root16 + P->par[TX_PTCH] * 256 + TX_INTERVAL[clampi(P->par[TX_INTV], 0, TX_NINTV - 1)][h] * 256
        + (((int32_t)(r1 & 0xFFFFu) - 32768) * cents >> 15);
    while (d16 > 3072) d16 -= 3072;                   /* the rate cap of the decoder: fold down an octave */
    while (d16 < -9216) d16 += 3072;
    step = tx_ratio(d16);
    span = (uint32_t)(((uint64_t)len * step) >> 16);
    if (n < 64u)
        return TX_RATE;
    if (span + 3u > n) {                              /* a short sample: a shorter pass */
        span = n - 3u;
        len = (uint32_t)(((uint64_t)span << 16) / step);
    }
    if (len < 64u)
        return TX_RATE;
    fade = len / 4u;                                  /* a quarter of the pass at each end */
    /* position: POS + EVOL, the head's own offset, DRFT jitters it by up to +-6 % */
    c = (int32_t)tx_position(P) + TX_HOFFS[h] * 256;
    jit = (((int32_t)(r2 & 0xFFFFu) - 32768) * P->par[TX_DRFT]) / (127 * 8);
    pos = (int32_t)(((uint64_t)(uint32_t)clampi(c + jit, 0, 65535) * (n - 1u)) >> 16) - (int32_t)(span / 2u);
    pos = clampi(pos, 0, (int32_t)(n - 3u - span));
    for (i = 0; i < TX_NP && !g; i++)
        if (!P->p[i].owner)
            g = &P->p[i];
    if (!g)
        return len - fade;
    if (late >= fade)
        late = fade - 1u;
    {
        uint64_t adv = (uint64_t)late * step;
        g->pos = (uint32_t)pos + (uint32_t)(adv >> 16);
        g->frac = (int32_t)(adv & 0xFFFFu);
    }
    g->step = step;
    g->len = len;
    g->left = len - late;
    g->fade = fade;
    /* heads are different stretches of the sample: their powers add */
    g->gain = heads >= 3u ? 18918 : heads == 2u ? 23170 : 32767;
    g->owner = (uint8_t)(vi + 1u);
    g->head = (uint8_t)h;
    return len - fade;
}

/* the window of a pass with `left` output samples to go, Q15 */
static int32_t tx_win(const tx_pass_t *g, uint32_t left)
{
    uint32_t done = g->len - left, k = done < left ? done : left, q;
    if (k >= g->fade)
        return 32767;
    q = (uint32_t)(((uint64_t)k << 15) / g->fade);    /* Q15 through the ramp */
    {
        uint32_t wi = q >> 8, wf = q & 255u;
        return TX_RAMP[wi] + (((TX_RAMP[wi + 1u] - TX_RAMP[wi]) * (int32_t)wf) >> 8);
    }
}

/* one pass into acc; the window is a linear ramp over the block between its exact end values */
static void tx_run(const tx_zone_t *z, tx_pass_t *g, int32_t *acc, uint32_t n)
{
    uint32_t i, m = n < g->left ? n : g->left, pos = g->pos, step = g->step;
    int32_t frac = g->frac, w0, w1, wq, dq;
    const int16_t *d = z->data;
    w0 = (tx_win(g, g->left) * g->gain) >> 15;
    g->left -= m;
    w1 = (tx_win(g, g->left) * g->gain) >> 15;
    wq = w0 << 10;
    dq = m ? ((w1 - w0) << 10) / (int32_t)m : 0;
    for (i = 0; i < m; i++) {
        int32_t a = d[pos], b = d[pos + 1u];
        int32_t s = a + (((b - a) * (frac >> 1)) >> 15);
        acc[i] += (s * (wq >> 10)) >> 15;
        wq += dq;
        frac += (int32_t)step;
        pos += (uint32_t)frac >> 16;
        frac &= 0xFFFF;
        if (pos + 1u >= z->n)
            pos = z->n - 2u;
    }
    g->pos = pos;
    g->frac = frac;
    if (!g->left)
        g->owner = 0;
}

void tx_note_on(tx_part_t *P, uint32_t vi, uint32_t note)
{
    tx_voice_t *v = &P->v[vi % TX_POLY];
    uint32_t i, h;
    vi %= TX_POLY;
    for (i = 0; i < TX_NP; i++)                       /* a fresh voice: no passes left from before */
        if (P->p[i].owner == vi + 1u)
            P->p[i].owner = 0;
    v->active = 1;
    v->note = (uint8_t)note;
    v->pitch16 = (int32_t)note << 8;
    v->lp = 0;
    /* the heads enter one after another, a quarter second apart, so a note swells into a texture */
    for (h = 0; h < TX_HEADS; h++)
        v->next[h] = (int32_t)(h * (TX_RATE / 4u));
}

void tx_note_off(tx_part_t *P, uint32_t vi)
{
    uint32_t i;
    vi %= TX_POLY;
    P->v[vi].active = 0;
    for (i = 0; i < TX_NP; i++)
        if (P->p[i].owner == vi + 1u)
            P->p[i].owner = 0;
}

void tx_render(tx_part_t *P, int32_t *out, uint32_t n)
{
    uint32_t vi, i, h, heads = tx_heads_for(P);
    int32_t acc[128], lp;
    if (n > 128u)
        n = 128u;
    /* EVOL's phases: 23 s and 37 s a turn */
    P->evo1 += 4234u * n;
    P->evo2 += 2632u * n;
    lp = 2000 + ((P->par[TX_TONE] * 30767) / 127);    /* one-pole low-pass coefficient, Q15 */
    for (vi = 0; vi < TX_POLY; vi++) {
        tx_voice_t *v = &P->v[vi];
        int32_t y = v->lp;
        int started = 0;
        if (!v->active)
            continue;
        for (i = 0; i < n; i++)
            acc[i] = 0;
        for (i = 0; i < TX_NP; i++)
            if (P->p[i].owner == vi + 1u)
                tx_run(&P->zone, &P->p[i], acc, n);
        /* the next passes: at most one start per voice and block (the device seeks its decoder there) */
        for (h = 0; h < TX_HEADS; h++) {
            v->next[h] -= (int32_t)n;
            if (h < heads && v->next[h] <= 0 && !started) {
                v->next[h] += (int32_t)tx_spawn(P, vi, h, heads, (uint32_t)-v->next[h]);
                started = 1;
            } else if (h >= heads && v->next[h] < 0)
                v->next[h] = 0;
        }
        for (i = 0; i < n; i++) {
            y += ((clampi(acc[i], -65535, 65535) - y) * lp) >> 15;
            out[i] += y;
        }
        v->lp = y;
    }
}
