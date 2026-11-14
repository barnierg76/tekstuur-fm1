/* Tekstuur voor de FM-1. Copyright (C) 2026 BARNIR. Alle rechten voorbehouden.
 *
 * Vier lagen (A..D) die elk een opgenomen bron bespelen: als langzaam verschuivende lussen (LOOP), als een wolk
 * korrels (GRAIN), uitgerekt tot hij bijna stilstaat (STRETCH) of als aanslag van meeklinkende snaren (RESON), gemengd met een vectorpunt dat uit zichzelf kan bewegen (Evolve), door chorus, delay en galm. De klankmotor staat
 * in dsp/tk_engine.c; dit bestand is het instrument eromheen: toetsen en MIDI, de bediening, de schermen, presets.
 *
 * Bediening:
 *   toetsen         spelen (aanslaggevoelig); OCT- / OCT+ het octaaf; PLAY houdt noten vast (nog eens: los)
 *   PRESET          kiest een preset (56 ingebouwd, 16 eigen); SAVE bewaart de klank van nu als eigen preset
 *   SELECT / SEL    kiest de laag A..D
 *   ALGORITHM       bladert door de pagina's; HOME, GLO, LFO, EDIT, ENV en FX springen er direct heen
 *   K1..K4          de vier regelaars van de pagina
 *   SEQ             FREEZE: de galm houdt vast wat erin zit, tot je nog eens drukt
 * Onderdeel van de unity-build van fm1-base (device/unity.c, host/plat_host.c), na het platform: gfx.c en announce.c
 * zijn in beeld. */
#include "dsp/tk_engine.h"
#include "dsp/tk_samples.h"
#include "dsp/tk_presets.h"
#include "dsp/tk_levels.h"
#include "tk_logo.h"

/* ------------------------------------------------------------- kleuren --- */
/* het donkere papier en de inkten van de plugin (Tekstuur src/ui/Style.h) */
#define K_BG RGB(0x12, 0x11, 0x18)
#define K_PANEL RGB(0x22, 0x20, 0x2c)
#define K_LINE RGB(0x34, 0x31, 0x42)
#define K_DIM RGB(0x7d, 0x78, 0x88)
#define K_PAPER RGB(0xf1, 0xe9, 0xda)
#define K_ORANGE RGB(0xf0, 0x8a, 0x5d)
#define K_PINK RGB(0xf2, 0x8b, 0x9b)
#define K_AQUA RGB(0x79, 0xcf, 0xe0)
static const uint8_t LAYER_RGB[TK_NL][3] = {{0xf2, 0x8b, 0x9b}, {0x79, 0xcf, 0xe0}, {0xf3, 0xc6, 0x4d}, {0xb3, 0xa3, 0xee}};
static uint16_t lcol[TK_NL][3];          /* per laag: vol, half, zacht */

static uint16_t shade(const uint8_t *c, uint32_t t)   /* t / 256 van de inkt op het papier */
{
    uint32_t r = (0x12u * (256u - t) + c[0] * t) >> 8, g = (0x11u * (256u - t) + c[1] * t) >> 8,
             b = (0x18u * (256u - t) + c[2] * t) >> 8;
    return RGB(r, g, b);
}

/* --------------------------------------------------------------- motor --- */
static tk_t T;
static int16_t dly_mem[2 * TK_DLY_MAX] PLAT_POOL;
static int16_t rv_mem[TK_RV_MEM] PLAT_POOL;
static int32_t res_mem[TK_NRES * TK_HEADS * TK_RES_LEN] PLAT_POOL;   /* de snaren van RESON */

/* De bronnen: de ingebouwde (tk_samples.h), en daarachter die uit de bank die de computer stuurde
 * (tools/tekstuur_samples.py): "TKB1", lengte, CRC-32 over de rest, aantal, per bron 24 bytes (naam, plek, samples,
 * frequentie, grondtoon), dan de samples zoals de ingebouwde. */
#define BANK_BASE 0x97000u
#define BANK_MAX (0xDC000u - BANK_BASE)
#define BANK_NSRC 8u
#define SRC_MAX (TK_NSOURCES + BANK_NSRC)
static tk_src_t SRC[SRC_MAX];
static char bank_name[BANK_NSRC][9];
static uint32_t nsrc = TK_NSOURCES;

static uint32_t rd32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t crc32_of(const uint8_t *p, uint32_t n)
{
    static const uint32_t t[16] = {0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u,
                                   0x5005713Cu, 0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u,
                                   0xA00AE278u, 0xBDBDF21Cu};
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        c = t[c & 15u] ^ c >> 4;
        c = t[c & 15u] ^ c >> 4;
    }
    return ~c;
}
static void bank_scan(void)              /* welke bronnen er nu zijn */
{
    const uint8_t *b = plat_data(BANK_BASE);
    uint32_t i, k, n = TK_NSOURCES;
    for (i = 0; i < TK_NSOURCES; i++)
        SRC[i] = TK_SOURCES[i];
    if (b && rd32(b) == 0x31424B54u) {               /* "TKB1" */
        uint32_t total = rd32(b + 4), cnt = b[12];
        if (total >= 16u && total <= BANK_MAX && cnt <= BANK_NSRC && 16u + 24u * cnt <= total &&
            crc32_of(b + 12, total - 12u) == rd32(b + 8))
            for (i = 0; i < cnt; i++) {
                const uint8_t *e = b + 16u + 24u * i;
                uint32_t off = rd32(e + 8), len = rd32(e + 12), rate = rd32(e + 16);
                if (len < 8000u || off < 16u + 24u * cnt || off > total || len / 2u * 3u > total - off || rate < 4000u || rate > 48000u)
                    continue;
                for (k = 0; k < 8u && e[k] > 32u && e[k] < 127u; k++)
                    bank_name[n - TK_NSOURCES][k] = (char)e[k];
                bank_name[n - TK_NSOURCES][k] = 0;
                SRC[n].d = b + off;
                SRC[n].len = len & ~1u;
                SRC[n].rate = rate;
                SRC[n].root = e[20] & 127u;
                SRC[n].name = bank_name[n - TK_NSOURCES];
                n++;
            }
    }
    nsrc = n;
    T.src = SRC;
    T.nsrc = n;
}

/* noten van de hoofdlus naar de audio-interrupt: noot | aanslag << 8 (aanslag 0 = los; noot 255 = alles los) */
#define EVQ 64u
static volatile uint16_t evq[EVQ];
static volatile uint32_t ev_w, ev_r;
static void ev_push(uint32_t note, uint32_t vel)
{
    evq[ev_w % EVQ] = (uint16_t)(note | vel << 8);
    ev_w++;
}

static volatile uint8_t midi_pc;         /* een programmawissel over MIDI: preset + 1 (de hoofdlus laadt hem) */
static volatile uint8_t midi_touch;      /* MIDI veranderde een regelaar: het scherm opnieuw */
static volatile uint32_t overloads;      /* zo vaak kwam een blok bijna te laat */
static uint8_t sus_on;
static uint32_t sus_held[4];

static void midi_in(void)                /* audio-interrupt */
{
    uint32_t pkt, i;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xF0u, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        if (st == 0x90u && d2) {
            sus_held[d1 >> 5] &= ~(1u << (d1 & 31u));
            tk_note_on(&T, d1, d2);
        } else if (st == 0x80u || st == 0x90u) {
            if (sus_on)
                sus_held[d1 >> 5] |= 1u << (d1 & 31u);
            else
                tk_note_off(&T, d1);
        } else if (st == 0xB0u) {
            if (d1 == 64u) {                         /* het pedaal */
                sus_on = d2 >= 64u;
                if (!sus_on)
                    for (i = 0; i < 128u; i++)
                        if (sus_held[i >> 5] >> (i & 31u) & 1u) {
                            sus_held[i >> 5] &= ~(1u << (i & 31u));
                            tk_note_off(&T, i);
                        }
            } else if (d1 == 1u) {                   /* het modulatiewiel: Evolve */
                T.p.evo = (uint8_t)d2;
                midi_touch = 1;
            } else if (d1 == 120u || d1 == 123u) {
                tk_all_off(&T);
                sus_held[0] = sus_held[1] = sus_held[2] = sus_held[3] = 0;
            }
        } else if (st == 0xC0u) {
            midi_pc = (uint8_t)(d1 + 1u);
        }
    }
}

/* De motor rekent op 22,05 kHz (de frequentie van de bronnen); hier wordt dat 44,1 kHz: elk sample van de motor, en
 * ertussen een sample uit zijn vier buren, (9 (b + c) - (a + d)) / 16. Daarna MASTER. */
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    static int32_t gq;                   /* MASTER, Q16, glijdend: draaien geeft geen stapjes */
    static int32_t eng[APP_BLOCK], h[2][3];
    uint32_t i, c;
    int32_t target = (int32_t)master_q12 << 4;
    midi_in();
    while (ev_r != ev_w) {
        uint32_t e = evq[ev_r % EVQ], note = e & 0xFFu, vel = e >> 8;
        ev_r++;
        if (note == 0xFFu)
            tk_all_off(&T);
        else if (vel)
            tk_note_on(&T, note, vel);
        else
            tk_note_off(&T, note);
    }
    tk_render(&T, eng, n / 2u);
    for (i = 0; i < n / 2u; i++) {
        int32_t g;
        gq += (target - gq) >> 6;
        g = gq >> 4;
        for (c = 0; c < 2u; c++) {
            int32_t x = eng[2u * i + c] >> 8, a = h[c][0], b = h[c][1], d = h[c][2];
            int32_t mid = (9 * (b + d) - (a + x)) >> 4;
            h[c][0] = b;
            h[c][1] = d;
            h[c][2] = x;
            lr[4u * i + c] = b * g >> 4;             /* 16 bits x Q12 -> 24 bits */
            lr[4u * i + 2u + c] = clamp(mid, -32768, 32767) * g >> 4;
        }
    }
}

void app_overload(void)                  /* audio-interrupt: het blok nam meer dan 90 % van zijn tijd */
{
    if (T.np_max > 12u)
        T.np_max = (uint8_t)(T.np_max - 4u);
    overloads++;
}

void app_panic(void)
{
    uint32_t i;
    for (i = 0; i < TK_NV; i++)
        T.v[i].on = 0;
    for (i = 0; i < TK_NP; i++)
        T.ps[i].on = 0;
}

/* ----------------------------------------------------------- bewaren --- */
#define NFACT ((uint32_t)TK_NPRESETS)
#define NUSER 16u
#define TKS_MAGIC 0x544B5332u             /* "TKS2" */
#define TKU_MAGIC 0x544B5532u             /* "TKU2": de klank kreeg resonantie en shimmer erbij */
typedef struct {
    uint32_t magic;
    int8_t octave;                       /* -3 .. +3 */
    uint8_t preset;                      /* 0 .. NFACT - 1 ingebouwd, daarna de eigen presets */
    uint8_t rsv[2];
} state_t;
typedef struct {
    uint32_t magic;
    uint8_t used[NUSER];
    tk_patch_t p[NUSER];
} user_t;
static state_t st;
static user_t user;

static void bytes_copy(void *d, const void *s, uint32_t n)
{
    uint8_t *a = d;
    const uint8_t *b = s;
    while (n--)
        *a++ = *b++;
}

/* --------------------------------------------------------- de pagina's --- */
enum { PG_PLAY, PG_MIX, PG_MOVE, PG_SRC, PG_PITCH, PG_TONE, PG_ENV, PG_FX, PG_FX2, NPG };
enum { K_NONE, K_VAL, K_SRC, K_ENG, K_INTV, K_PITCH };
enum { W_GLOBAL, W_LAYER, W_L0, W_L1, W_L2, W_L3 };   /* de klank, de gekozen laag, of een vaste laag */
typedef struct {
    const char *name;
    uint8_t where, off, kind;
} par_t;
#define GP(f) (uint8_t)__builtin_offsetof(tk_patch_t, f)
#define LP(f) (uint8_t)__builtin_offsetof(tk_layer_t, f)
static const char *const PAGE_NAME[NPG] = {"PLAY", "MIX", "MOVE", "SOURCE", "PITCH", "TONE", "ENVELOPE", "EFFECTS", "REVERB"};
static const par_t PAGE[NPG][4] = {
    {{"VECTOR X", W_GLOBAL, GP(vx), K_VAL}, {"VECTOR Y", W_GLOBAL, GP(vy), K_VAL}, {"EVOLVE", W_GLOBAL, GP(evo), K_VAL},
     {"TIME", W_GLOBAL, GP(evo_time), K_VAL}},
    {{"LEVEL A", W_L0, LP(level), K_VAL}, {"LEVEL B", W_L1, LP(level), K_VAL}, {"LEVEL C", W_L2, LP(level), K_VAL},
     {"LEVEL D", W_L3, LP(level), K_VAL}},
    {{"EVOLVE A", W_L0, LP(evo), K_VAL}, {"EVOLVE B", W_L1, LP(evo), K_VAL}, {"EVOLVE C", W_L2, LP(evo), K_VAL},
     {"EVOLVE D", W_L3, LP(evo), K_VAL}},
    {{"SOURCE", W_LAYER, LP(src), K_SRC}, {"ENGINE", W_LAYER, LP(eng), K_ENG}, {"POSITION", W_LAYER, LP(pos), K_VAL},
     {"LENGTH", W_LAYER, LP(len), K_VAL}},
    {{"INTERVAL", W_LAYER, LP(intv), K_INTV}, {"PITCH", W_LAYER, LP(ptch), K_PITCH}, {"DENSITY", W_LAYER, LP(dens), K_VAL},
     {"SPREAD", W_LAYER, LP(sprd), K_VAL}},
    {{"CUTOFF", W_LAYER, LP(cut), K_VAL}, {"RESONANCE", W_LAYER, LP(res), K_VAL}, {"DETUNE", W_LAYER, LP(detune), K_VAL},
     {"WIDTH", W_LAYER, LP(width), K_VAL}},
    {{"ATTACK", W_LAYER, LP(atk), K_VAL}, {"RELEASE", W_LAYER, LP(rel), K_VAL}, {"REVERB", W_LAYER, LP(rev), K_VAL},
     {"DELAY", W_LAYER, LP(dly), K_VAL}},
    {{"CHORUS", W_GLOBAL, GP(cho), K_VAL}, {"DLY TIME", W_GLOBAL, GP(dly_time), K_VAL}, {"FEEDBACK", W_GLOBAL, GP(dly_fb), K_VAL},
     {"VOLUME", W_GLOBAL, GP(master), K_VAL}},
    {{"SIZE", W_GLOBAL, GP(rev_size), K_VAL}, {"DAMPING", W_GLOBAL, GP(rev_damp), K_VAL},
     {"SHIMMER", W_GLOBAL, GP(shim), K_VAL}, {"", 0, 0, K_NONE}},
};
static const char *const ENG_NAME[TK_NENG] = {"OFF", "LOOP", "GRAIN", "STRETCH", "RESON"};
static const char *const INTV_NAME[TK_NINTV] = {"UNISON", "OCTAVE", "FIFTH", "OCT+5", "SUB"};
static const char LAYER_LETTER[TK_NL][2] = {"A", "B", "C", "D"};

/* --------------------------------------------------------------------- UI --- */
static uint8_t *par_ptr(const par_t *q);
#define WF_W 68u                         /* de golfvorm van een bron op het scherm: zoveel kolommen */
static uint8_t wave_pk[SRC_MAX][WF_W];

static struct {
    char msg[2][24];
    uint32_t msg_until;
    uint32_t keys, btn;                  /* ingedrukt bij de vorige invoer */
    uint32_t latch[4];                   /* PLAY: de vastgehouden noten */
    uint32_t mid_at, recover_at;
    uint8_t page, layer, preset, last_k;
    uint8_t hold, dirty, freeze;
    uint8_t rd_head, rd_mid, rd_bot;     /* dit deel van het scherm opnieuw */
} ui;

static void text_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}
static void int_text(int32_t v, char *b)
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}
static void two_digits(uint32_t v, char *b)
{
    b[0] = (char)('0' + v / 10u % 10u);
    b[1] = (char)('0' + v % 10u);
    b[2] = 0;
}

void app_say(const char *a, const char *b)
{
    uint32_t i;
    char t[48];
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
    ui.rd_head = 1;
    if (ann_on()) {
        t[0] = 0;
        text_cat(t, ui.msg[0]);
        if (ui.msg[1][0]) {
            text_cat(t, " ");
            text_cat(t, ui.msg[1]);
        }
        ann_say(t, 0, ANN_NOW);
    }
}

static void preset_name(uint32_t i, char *t)     /* "03 KATHEDRAAL", "U02 USER" */
{
    if (i < NFACT) {
        two_digits(i + 1u, t);
        text_cat(t, " ");
        text_cat(t, TK_PRESETS[i].name);
    } else {
        t[0] = 'U';
        two_digits(i - NFACT + 1u, t + 1);
        text_cat(t, " USER");
    }
}

static void preset_load(uint32_t i)
{
    if (i >= NFACT + NUSER || (i >= NFACT && !user.used[i - NFACT]))
        i = 0;
    bytes_copy(&T.p, i < NFACT ? &TK_PRESETS[i].p : &user.p[i - NFACT], sizeof T.p);
    if (i < NFACT)
        T.p.master = TK_LEVELS[i];                   /* elke ingebouwde preset even hard (tk_levels.h) */
    ui.preset = (uint8_t)i;
    ui.dirty = 0;
    ui.rd_head = ui.rd_mid = ui.rd_bot = 1;
}

static void preset_step(int32_t d)
{
    uint32_t i = ui.preset, n = NFACT + NUSER, k;
    char t[24];
    for (k = 0; k < n; k++) {
        i = (i + (d > 0 ? 1u : n - 1u)) % n;
        if (i < NFACT || user.used[i - NFACT])
            break;
    }
    preset_load(i);
    preset_name(ui.preset, t);
    if (ann_on())
        ann_say(t, 0, ANN_NOW);
}

static void preset_save(void)
{
    uint32_t slot = NUSER, i;
    char t[8];
    if (ui.preset >= NFACT) {
        slot = ui.preset - NFACT;
    } else {
        for (i = 0; i < NUSER; i++)
            if (!user.used[i]) {
                slot = i;
                break;
            }
    }
    if (slot >= NUSER) {
        app_say("USER PRESETS", "FULL");
        return;
    }
    bytes_copy(&user.p[slot], &T.p, sizeof T.p);
    user.used[slot] = 1;
    user.magic = TKU_MAGIC;
    st.magic = TKS_MAGIC;
    st.preset = (uint8_t)(NFACT + slot);
    if (plat_store_save(OBJ_USER, &user, sizeof user) != 0 || plat_store_save(OBJ_STATE, &st, sizeof st) != 0) {
        app_say("SAVE", "FAILED");
        return;
    }
    ui.preset = st.preset;
    ui.dirty = 0;
    t[0] = 'U';
    two_digits(slot + 1u, t + 1);
    app_say("SAVED", t);
}

static void waves(void)                  /* de golfvorm van elke bron, voor het scherm */
{
    uint32_t s, c;
    for (s = 0; s < nsrc; s++)
        for (c = 0; c < WF_W; c++) {
            uint32_t a = SRC[s].len * c / WF_W, b = SRC[s].len * (c + 1u) / WF_W;
            uint32_t pk = tk_src_peak(&SRC[s], a, b) >> 2;
            wave_pk[s][c] = (uint8_t)(pk > 255u ? 255u : pk);
        }
}

void app_init(void)
{
    uint32_t s;
    bank_scan();
    tk_init(&T, SRC, nsrc, dly_mem, rv_mem, res_mem);
    plat_usb_audio_full(1);              /* over USB altijd op vol niveau; MASTER regelt alleen speaker en koptelefoon */
    waves();
    if (plat_store_load(OBJ_USER, &user, sizeof user) != (int)sizeof user || user.magic != TKU_MAGIC) {
        for (s = 0; s < sizeof user; s++)
            ((uint8_t *)&user)[s] = 0;
        user.magic = TKU_MAGIC;
    }
    if (plat_store_load(OBJ_STATE, &st, sizeof st) != (int)sizeof st || st.magic != TKS_MAGIC || st.octave < -3 || st.octave > 3) {
        st.magic = TKS_MAGIC;
        st.octave = 0;
        st.preset = 0;
    }
    preset_load(st.preset);
}

/* ------------------------------------------------------------- regelaars --- */
static uint8_t *par_ptr(const par_t *q)
{
    if (q->where == W_GLOBAL)
        return (uint8_t *)&T.p + q->off;
    return (uint8_t *)&T.p.l[q->where == W_LAYER ? ui.layer : q->where - W_L0] + q->off;
}
static const char *par_name(uint32_t pg, uint32_t k)   /* een regelaar heet naar wat hij in deze engine doet */
{
    uint32_t e = T.p.l[ui.layer].eng;
    if (pg == PG_SRC && k == 3u && e == TK_RESON)
        return "RING";
    if (pg == PG_PITCH && k == 2u)
        return e == TK_STRETCH ? "SPEED" : e == TK_RESON ? "EXCITE" : e == TK_LOOP ? "-" : "DENSITY";
    return PAGE[pg][k].name;
}
static void par_text(const par_t *q, char *t)
{
    uint32_t v = *par_ptr(q);
    t[0] = 0;
    switch (q->kind) {
    case K_SRC: text_cat(t, SRC[v % nsrc].name); break;
    case K_ENG: text_cat(t, ENG_NAME[v % TK_NENG]); break;
    case K_INTV: text_cat(t, INTV_NAME[v % TK_NINTV]); break;
    case K_PITCH:
        if ((int8_t)v > 0)
            text_cat(t, "+");
        int_text((int8_t)v, t + (t[0] ? 1 : 0));
        break;
    case K_VAL: int_text((int32_t)v, t); break;
    default: break;
    }
}
static void par_turn(uint32_t k, int32_t e)
{
    const par_t *q = &PAGE[ui.page][k];
    uint8_t *p;
    int32_t v, one = e > 0 ? 1 : -1, a = e < 0 ? -e : e;
    if (q->kind == K_NONE)
        return;
    p = par_ptr(q);
    switch (q->kind) {
    case K_SRC: v = clamp(*p % (int32_t)nsrc + one, 0, (int32_t)nsrc - 1); break;
    case K_ENG: v = clamp(*p + one, 0, TK_NENG - 1); break;
    case K_INTV: v = clamp(*p + one, 0, TK_NINTV - 1); break;
    case K_PITCH: v = clamp((int8_t)*p + one, -24, 24); break;
    default: v = clamp(*p + e * (a >= 3 ? 4 : a == 2 ? 2 : 1), 0, 127); break;   /* snel draaien: grotere stappen */
    }
    if ((uint8_t)v != *p) {
        *p = (uint8_t)v;
        ui.dirty = 1;
        ui.rd_head = 1;
        if (ann_on()) {
            char t[16];
            par_text(q, t);
            ann_say(par_name(ui.page, k), t, ANN_NOW);
        }
    }
    ui.last_k = (uint8_t)k;
    ui.rd_bot = ui.rd_mid = 1;
}

static void page_set(uint32_t pg)
{
    ui.page = (uint8_t)(pg % NPG);
    ui.last_k = 255;
    ui.rd_bot = 1;
    if (ann_on())
        ann_say(PAGE_NAME[ui.page], 0, ANN_NOW);
}
static void layer_set(int32_t l)
{
    ui.layer = (uint8_t)clamp(l, 0, TK_NL - 1);
    ui.rd_mid = ui.rd_bot = 1;
    if (ann_on())
        ann_say("LAYER", LAYER_LETTER[ui.layer], ANN_NOW);
}

/* ---------------------------------------------------------------- noten --- */
static int32_t key_note(uint32_t k) { return clamp(53 + (int32_t)k + 12 * st.octave, 0, 127); }
static void note_send(int32_t note, uint32_t vel)   /* naar de motor en naar USB-MIDI */
{
    ev_push((uint32_t)note, vel);
    if (vel)
        plat_midi_send(0x09u | 0x90u << 8 | (uint32_t)note << 16 | vel << 24, TO_USB);
    else
        plat_midi_send(0x08u | 0x80u << 8 | (uint32_t)note << 16, TO_USB);
}
static void latch_clear(void)
{
    uint32_t i;
    for (i = 0; i < 128u; i++)
        if (ui.latch[i >> 5] >> (i & 31u) & 1u)
            note_send((int32_t)i, 0);
    ui.latch[0] = ui.latch[1] = ui.latch[2] = ui.latch[3] = 0;
}
static void octave_step(int32_t d)
{
    int32_t o = clamp(st.octave + d, -3, 3);
    uint32_t k;
    char t[4];
    if (o == st.octave)
        return;
    if (!ui.hold)                        /* een toets die ingedrukt blijft zou zijn noot niet meer loslaten */
        for (k = 0; k < NKEYS; k++)
            if (ui.keys >> k & 1u)
                note_send(key_note(k), 0);
    st.octave = (int8_t)o;
    t[0] = o < 0 ? '-' : '+';
    t[1] = (char)('0' + (o < 0 ? -o : o));
    t[2] = 0;
    app_say("OCTAVE", t);
    ui.rd_mid = 1;
}

static void input(void)
{
    uint32_t keys, btn = plat_buttons(), k, lit = 0, klit;
    int32_t e;
    touch_poll();
    keys = touch_keys();
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        int32_t note = key_note(k);
        if ((keys & bit) && !(ui.keys & bit)) {
            uint32_t v = touch_vel(k);
            if (!v)
                v = 100;
            if (ui.hold) {                           /* vasthouden: een toets zet zijn noot aan, nog eens: uit */
                if (ui.latch[note >> 5] >> (note & 31) & 1u) {
                    ui.latch[note >> 5] &= ~(1u << (note & 31));
                    note_send(note, 0);
                } else {
                    ui.latch[note >> 5] |= 1u << (note & 31);
                    note_send(note, v);
                }
            } else {
                note_send(note, v);
            }
        } else if (!(keys & bit) && (ui.keys & bit) && !ui.hold) {
            note_send(note, 0);
        }
    }
    for (k = 0; k < NB; k++) {
        uint32_t bit = 1u << k;
        if (!(btn & bit) || (ui.btn & bit))
            continue;
        switch (k) {
        case B_OCTDN: octave_step(-1); break;
        case B_OCTUP: octave_step(1); break;
        case B_HOME: page_set(PG_PLAY); break;
        case B_GLO: page_set(PG_MIX); break;
        case B_LFO: page_set(PG_MOVE); break;
        case B_EDIT: page_set(ui.page >= PG_SRC && ui.page < PG_TONE ? ui.page + 1u : PG_SRC); break;
        case B_ENV: page_set(PG_ENV); break;
        case B_FX: page_set(ui.page == PG_FX ? PG_FX2 : PG_FX); break;
        case B_SEL: layer_set((ui.layer + 1) % TK_NL); break;
        case B_SAVE: preset_save(); break;
        case B_SEQ:                                  /* de galm houdt vast wat erin zit */
            ui.freeze = (uint8_t)!ui.freeze;
            T.freeze = ui.freeze;
            app_say("FREEZE", ui.freeze ? "ON" : "OFF");
            ui.rd_mid = 1;
            break;
        case B_REC: {                                /* de toestand van het apparaat: belasting, haperingen, ruimte */
            char t[24], n[12];
            t[0] = 0;
            int_text((int32_t)plat_cpu_pct(), n);
            text_cat(t, n);
            text_cat(t, "% X");
            int_text((int32_t)plat_xruns(), n);
            text_cat(t, n);
            text_cat(t, " O");
            int_text((int32_t)overloads, n);
            text_cat(t, n);
            text_cat(t, " P");
            int_text((int32_t)T.np_max, n);
            text_cat(t, n);
            app_say("", t);
            ui.msg_until = plat_ms() + 15000u;
            break;
        }
        case B_PLAY:
            ui.hold = (uint8_t)!ui.hold;
            if (ui.hold) {                           /* wat nu klinkt blijft klinken */
                uint32_t j;
                for (j = 0; j < NKEYS; j++)
                    if (keys >> j & 1u) {
                        int32_t n = key_note(j);
                        ui.latch[n >> 5] |= 1u << (n & 31);
                    }
            } else {
                latch_clear();
            }
            app_say("HOLD", ui.hold ? "ON" : "OFF");
            ui.rd_mid = 1;
            break;
        default: break;
        }
    }
    if ((e = plat_enc(EN_SELECT)) != 0)
        layer_set(ui.layer + (e > 0 ? 1 : -1));
    if ((e = plat_enc(EN_ALGO)) != 0)
        page_set((uint32_t)(ui.page + (e > 0 ? 1 : (int32_t)NPG - 1)));
    if ((e = plat_enc(EN_PRESET)) != 0)
        preset_step(e);
    for (k = 0; k < 4u; k++)
        if ((e = plat_enc((int)(EN_K1 + k))) != 0)
            par_turn(k, e);
    ui.keys = keys;
    ui.btn = btn;
    /* de lampjes: de knop van de pagina, SAVE als de klank veranderd is, PLAY bij vasthouden, de toetsen die klinken */
    lit = btn;
    lit |= ui.page == PG_PLAY ? 1u << B_HOME : ui.page == PG_MIX ? 1u << B_GLO : ui.page == PG_MOVE ? 1u << B_LFO
           : ui.page <= PG_TONE ? 1u << B_EDIT : ui.page == PG_ENV ? 1u << B_ENV : 1u << B_FX;
    if (ui.dirty)
        lit |= 1u << B_SAVE;
    if (ui.hold)
        lit |= 1u << B_PLAY;
    if (ui.freeze)
        lit |= 1u << B_SEQ;
    klit = keys;
    for (k = 0; k < NKEYS; k++) {
        int32_t n = key_note(k);
        if (ui.latch[n >> 5] >> (n & 31) & 1u)
            klit |= 1u << k;
    }
    plat_glow(1);
    plat_leds(lit, klit);
}

void app_input_only(void) { input(); }

/* De computer stuurt een bank met eigen bronnen (cmd 0x60: lengte, CRC-32; lengte 0 haalt hem weg). Het geluid stopt,
 * de bank wordt sector voor sector gewist en geschreven met wat de computer aanreikt, en daarna nagekeken. Het
 * antwoord is een laatste vraag aan de computer: 0xC0000000 + het aantal bronnen, of 0xCF000000 + wat er misging. */
static void draw_head(void);
void app_usb_cmd(const uint8_t *d, uint32_t n)
{
    static uint8_t buf[512];
    uint32_t len, crc, off, code = 0;
    const uint8_t *b;
    if (n != 15u || d[2] != 0x60u)
        return;
    len = rd32(d + 6);
    crc = rd32(d + 10);
    if (len > BANK_MAX) {
        code = 1;
    } else {
        latch_clear();
        ui.hold = 0;
        nsrc = T.nsrc = TK_NSOURCES;                 /* niets leest de bank nog */
        app_panic();
        app_say("SAMPLES", len ? "LOADING" : "CLEARING");
        draw_head();
        plat_host_patience(3000u, 0);
        if (plat_data_erase(BANK_BASE) != 0)         /* de kop weg: vanaf nu is er geen bank, tot hij er heel staat */
            code = 2;
        for (off = 0; !code && off < len; off += 512u) {
            uint32_t m = len - off < 512u ? len - off : 512u;
            if (off && !(off & 0xFFFu) && plat_data_erase(BANK_BASE + off) != 0)
                code = 2;
            else if (plat_host_read(off, buf, m) != 0)
                code = 3;
            else if (plat_data_write(BANK_BASE + off, buf, m) != 0)
                code = 4;
        }
        b = plat_data(BANK_BASE);
        if (!code && len && (!b || crc32_of(b, len) != crc))
            code = 5;
        if (code)
            (void)plat_data_erase(BANK_BASE);
        bank_scan();
        if (!code && len && nsrc == TK_NSOURCES)
            code = 5;
        waves();
        app_say("SAMPLES", code ? "FAILED" : len ? "LOADED" : "CLEARED");
        ui.rd_head = ui.rd_mid = ui.rd_bot = 1;
    }
    (void)plat_host_read(code ? 0xCF000000u | code : 0xC0000000u | (nsrc - TK_NSOURCES), buf, 8u);
}

/* --------------------------------------------------------------- tekenen --- */
static void box(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)   /* een omtrek */
{
    cv_rect(x, y, w, 1, c);
    cv_rect(x, y + h - 1, w, 1, c);
    cv_rect(x, y, 1, h, c);
    cv_rect(x + w - 1, y, 1, h, c);
}

static void draw_head(void)              /* 240 x 28: het woordmerk, de preset of een bericht */
{
    char t[28];
    int32_t w;
    cv_begin(240, 28, K_BG);
    cv_text(7, 7, &FONT_B, APP_NAME, K_PINK);        /* twee inkten net naast elkaar gedrukt, het wit eroverheen */
    cv_text(10, 5, &FONT_B, APP_NAME, K_AQUA);
    cv_text(8, 6, &FONT_B, APP_NAME, K_PAPER);
    if (plat_ms() < ui.msg_until) {
        w = text_w(&FONT_B, ui.msg[1]);
        cv_text(234 - w, 6, &FONT_B, ui.msg[1], K_ORANGE);
        cv_text(234 - w - 6 - text_w(&FONT_B, ui.msg[0]), 6, &FONT_B, ui.msg[0], K_DIM);
    } else {
        preset_name(ui.preset, t);
        const felucca_font_t *f = text_w(&FONT_B, t) > 222 - 14 - text_w(&FONT_B, APP_NAME) ? &FONT_T : &FONT_B;   /* een lange naam: klein */
        w = text_w(f, t);
        cv_text(234 - w, f == &FONT_B ? 6 : 10, f, t, K_PAPER);
        if (ui.dirty)                                /* veranderd sinds het laden of bewaren */
            cv_rect(234 - w - 9, 12, 4, 4, K_ORANGE);
    }
    cv_rect(0, 27, 240, 1, K_LINE);
    cv_blit(0, 0);
}

static void draw_mid(void)               /* 240 x 116: de vier lagen, het vectorvlak, de toestand */
{
    uint32_t li, i, c;
    char t[12];
    int32_t px0 = 158, py0 = 4, x, y;
    cv_begin(240, 116, K_BG);
    for (li = 0; li < TK_NL; li++) {
        const tk_layer_t *L = &T.p.l[li];
        const tk_src_t *s = &SRC[L->src % nsrc];
        int32_t y0 = 2 + 28 * (int32_t)li, x0 = 80;
        int on = L->eng != TK_OFF;
        if (li == ui.layer)
            cv_rect(2, y0, 150, 26, K_PANEL);
        cv_rect(2, y0, 3, 26, on ? lcol[li][0] : lcol[li][2]);
        cv_text(10, y0 + 5, &FONT_B, LAYER_LETTER[li], on ? lcol[li][0] : K_DIM);
        cv_text(26, y0 + 1, &FONT_T, s->name, on ? K_PAPER : K_DIM);
        cv_text(26, y0 + 13, &FONT_T, ENG_NAME[L->eng % TK_NENG], K_DIM);
        for (c = 0; c < WF_W; c++) {
            int32_t h = wave_pk[L->src % nsrc][c] * 11 / 255;
            cv_rect(x0 + (int32_t)c, y0 + 12 - h, 1, 2 * h + 1, on ? lcol[li][1] : K_LINE);
        }
        if (!on)
            continue;
        x = x0 + L->pos * (int32_t)(WF_W - 1u) / 127;
        cv_rect(x - 1, y0 + 24, 3, 2, K_PAPER);      /* de plek */
        for (i = 0; i < TK_NP; i++) {                /* wat er nu klinkt: een streep per doorgang */
            const tk_pass_t *p = &T.ps[i];
            if (p->on && p->layer == li && p->g0 > 1200) {
                uint32_t at = p->pos >> 12;
                if (at >= s->len)
                    at = s->len - 1u;
                cv_rect(x0 + (int32_t)(at * (WF_W - 1u) / s->len), y0 + 2, 1, 21, p->g0 > 9000 ? lcol[li][0] : K_PAPER);
            }
        }
    }
    /* het vectorvlak: een hoek is een laag, het midden alles; het ringetje is de stand, de stip waar Evolve nu is */
    box(px0, py0, 78, 78, K_LINE);
    cv_rect(px0 + 39, py0 + 1, 1, 76, K_PANEL);
    cv_rect(px0 + 1, py0 + 39, 76, 1, K_PANEL);
    for (li = 0; li < TK_NL; li++)
        cv_text(px0 + (li & 1u ? 68 : 4), py0 + (li & 2u ? 64 : 2), &FONT_T, LAYER_LETTER[li],
                T.p.l[li].eng != TK_OFF ? lcol[li][0] : lcol[li][2]);
    x = px0 + 4 + T.p.vx * 69 / 127;
    y = py0 + 4 + T.p.vy * 69 / 127;
    box(x - 3, y - 3, 7, 7, K_PAPER);
    x = px0 + 4 + T.vx_now * 69 / 127;
    y = py0 + 4 + T.vy_now * 69 / 127;
    cv_rect(x - 2, y - 2, 5, 5, K_ORANGE);
    /* de toestand: octaaf, vasthouden, belasting */
    cv_text(px0, 87, &FONT_T, "OCT", K_DIM);
    t[0] = st.octave < 0 ? '-' : '+';
    t[1] = (char)('0' + (st.octave < 0 ? -st.octave : st.octave));
    t[2] = 0;
    cv_text(px0 + 22, 87, &FONT_T, t, K_PAPER);
    cv_text(px0 + 38, 87, &FONT_T, "HLD", ui.hold ? K_ORANGE : K_LINE);
    cv_text(px0 + 60, 87, &FONT_T, "FRZ", ui.freeze ? K_AQUA : K_LINE);
    cv_text(px0, 101, &FONT_T, "CPU", K_DIM);
    int_text((int32_t)plat_cpu_pct(), t);
    cv_text(px0 + 22, 101, &FONT_T, t, plat_cpu_pct() > 85u ? K_ORANGE : K_PAPER);
    int_text((int32_t)tk_passes(&T), t);
    cv_text(px0 + 78 - text_w(&FONT_T, t), 101, &FONT_T, t, K_DIM);
    cv_blit(0, 28);
}

static void draw_bot(void)               /* 240 x 96: de pagina en haar vier regelaars */
{
    uint32_t k;
    int32_t x = 6;
    int layer_page = ui.page >= PG_SRC && ui.page <= PG_ENV;
    cv_begin(240, 96, K_BG);
    cv_rect(0, 0, 240, 1, K_LINE);
    if (layer_page) {                                /* de laag waar de regelaars over gaan */
        cv_rect(6, 5, 16, 18, lcol[ui.layer][0]);
        cv_text(10, 6, &FONT_B, LAYER_LETTER[ui.layer], K_BG);
        x = 28;
    }
    cv_text(x, 6, &FONT_B, PAGE_NAME[ui.page], K_PAPER);
    for (k = 0; k < NPG; k++)
        cv_rect(234 - (int32_t)(NPG - k) * 7, 12, 4, 4, k == ui.page ? K_ORANGE : K_LINE);
    for (k = 0; k < 4u; k++) {
        const par_t *q = &PAGE[ui.page][k];
        int32_t cx = 4 + (int32_t)k * 59, v, w;
        uint16_t ink;
        char t[16];
        static const char *const KN[4] = {"K1", "K2", "K3", "K4"};
        cv_text(cx, 78, &FONT_T, KN[k], q->kind == K_NONE ? K_LINE : K_DIM);
        if (q->kind == K_NONE)
            continue;
        ink = q->where == W_GLOBAL ? K_ORANGE : lcol[q->where == W_LAYER ? ui.layer : q->where - W_L0][0];
        cv_text(cx, 28, &FONT_T, par_name(ui.page, k), k == ui.last_k ? K_PAPER : K_DIM);
        par_text(q, t);
        cv_text(cx, 42, &FONT_B, t, K_PAPER);
        cv_rect(cx, 66, 56, 5, K_LINE);
        v = *par_ptr(q);
        switch (q->kind) {
        case K_SRC: w = (v % (int32_t)nsrc + 1) * 56 / (int32_t)nsrc; cv_rect(cx, 66, w, 5, ink); break;
        case K_ENG: w = v * 56 / (TK_NENG - 1); cv_rect(cx, 66, w, 5, ink); break;
        case K_INTV: w = (v + 1) * 56 / TK_NINTV; cv_rect(cx, 66, w, 5, ink); break;
        case K_PITCH:                                /* vanuit het midden */
            v = (int8_t)v;
            w = (v < 0 ? -v : v) * 28 / 24;
            cv_rect(v < 0 ? cx + 28 - w : cx + 28, 66, w ? w : 1, 5, ink);
            break;
        default: w = v * 56 / 127; cv_rect(cx, 66, w, 5, ink); break;
        }
    }
    cv_blit(0, 144);
}

void app_splash(void)                    /* het woordmerk op donker papier, de maker en de versie eronder */
{
    uint32_t i, j;
    uint16_t paper = TK_LOGO_PAL[0];
    lcd_fill(0, 0, 240, 240, paper);
    cv_begin(240, TK_LOGO_H, paper);
    for (j = 0; j < TK_LOGO_H; j++)
        for (i = 0; i < TK_LOGO_W; i++) {
            uint32_t b = TK_LOGO_PX[(j * TK_LOGO_W + i) >> 1], v = i & 1u ? b & 15u : b >> 4;
            if (v)
                cv_pset(4 + (int32_t)i, (int32_t)j, TK_LOGO_PAL[v]);
        }
    cv_blit(0, 56);
    cv_begin(240, 44, paper);
    cv_text(10, 0, &FONT_B, "by BARNIR", K_PAPER);
    cv_text(10, 24, &FONT_T, "FIRMWARE " APP_VERSION, K_DIM);
    cv_blit(0, 176);
}

void app_redraw(void) { ui.rd_head = ui.rd_mid = ui.rd_bot = 1; }   /* de spiegel over USB wil het hele scherm */

void app_ui_init(void)
{
    uint32_t li;
    touch_on(1);
    for (li = 0; li < TK_NL; li++) {
        lcol[li][0] = shade(LAYER_RGB[li], 256);
        lcol[li][1] = shade(LAYER_RGB[li], 120);
        lcol[li][2] = shade(LAYER_RGB[li], 56);
    }
    lcd_fill(0, 0, 240, 240, K_BG);
    ui.last_k = 255;
    ui.rd_head = ui.rd_mid = ui.rd_bot = 1;
}

static void speak(void)                  /* een schermlezer die net meeluistert hoort waar je bent */
{
    char t[80], n[28];
    if (ann_on() && ann_where()) {
        t[0] = 0;
        text_cat(t, APP_NAME ". PRESET ");
        preset_name(ui.preset, n);
        text_cat(t, n);
        text_cat(t, ". PAGE ");
        text_cat(t, PAGE_NAME[ui.page]);
        ann_say(t, 0, ANN_NOW);
    }
    ann_tick(plat_ms());
}

void app_frame(void)
{
    uint32_t now = plat_ms();
    input();
    speak();
    if (midi_pc) {                                   /* een programmawissel over MIDI */
        uint32_t i = midi_pc - 1u;
        midi_pc = 0;
        if (i < NFACT || (i < NFACT + NUSER && user.used[i - NFACT]))
            preset_load(i);
    }
    if (midi_touch) {
        midi_touch = 0;
        ui.rd_bot = 1;
    }
    if (ui.msg_until && now >= ui.msg_until) {
        ui.msg_until = 0;
        ui.rd_head = 1;
    }
    if (now - ui.recover_at >= 10000u) {             /* na een overbelasting komt de ruimte langzaam terug */
        ui.recover_at = now;
        if (T.np_max < TK_NP && plat_cpu_pct() < 50u)
            T.np_max = (uint8_t)(T.np_max + 2u);
    }
    if (now - ui.mid_at >= (tk_passes(&T) ? 66u : 250u))   /* de strepen bewegen: 15 keer per seconde */
        ui.rd_mid = 1;
    if (ui.rd_head) {
        ui.rd_head = 0;
        draw_head();
    }
    if (ui.rd_mid) {
        ui.rd_mid = 0;
        ui.mid_at = now;
        draw_mid();
    }
    if (ui.rd_bot) {
        ui.rd_bot = 0;
        draw_bot();
    }
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "octave"))
        *got = st.octave;
    else if (!strcmp(what, "preset"))
        *got = ui.preset;
    else if (!strcmp(what, "page"))
        *got = ui.page;
    else if (!strcmp(what, "layer"))
        *got = ui.layer;
    else if (!strcmp(what, "dirty"))
        *got = ui.dirty;
    else if (!strcmp(what, "hold"))
        *got = ui.hold;
    else if (!strcmp(what, "freeze"))
        *got = T.freeze;
    else if (!strcmp(what, "passes"))
        *got = (int)tk_passes(&T);
    else if (!strcmp(what, "voices")) {
        uint32_t i;
        for (*got = 0, i = 0; i < TK_NV; i++)
            *got += T.v[i].on;
    } else if (!strcmp(what, "held")) {
        uint32_t i;
        for (*got = 0, i = 0; i < TK_NV; i++)
            *got += T.v[i].on && T.v[i].held;
    } else if (!strcmp(what, "users")) {
        uint32_t i;
        for (*got = 0, i = 0; i < NUSER; i++)
            *got += user.used[i];
    } else if (!strncmp(what, "par", 3) && what[3] >= '0' && what[3] <= '3')   /* parN: regelaar N van de pagina */
        *got = PAGE[ui.page][what[3] - '0'].kind == K_PITCH ? (int8_t)*par_ptr(&PAGE[ui.page][what[3] - '0'])
                                                           : *par_ptr(&PAGE[ui.page][what[3] - '0']);
    else if (!strcmp(what, "sources"))
        *got = (int)nsrc;
    else if (!strcmp(what, "pool"))
        *got = T.np_max;
    else
        return 0;
    return 1;
}
#endif
