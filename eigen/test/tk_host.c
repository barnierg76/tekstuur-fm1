/* De Tekstuur-motor op de Mac: metingen en luistervoorbeelden.
 *   build/tk_host            metingen
 *   build/tk_host demo DIR   luistervoorbeelden (WAV, stereo) in DIR */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "../dsp/tk_engine.h"
#include "../dsp/tk_samples.h"
#include "../dsp/tk_presets.h"
#if __has_include("../dsp/tk_levels.h")
#include "../dsp/tk_levels.h"
#else
static const uint8_t TK_LEVELS[TK_NPRESETS];
#endif

static tk_t T;
static int16_t dly_mem[2 * TK_DLY_MAX], rv_mem[TK_RV_MEM]; static int32_t res_mem[TK_NRES * TK_HEADS * TK_RES_LEN];
static int fails;
static void check(const char *what, int ok, double a, double b)
{
    printf("%-64s %-4s (%.3f, %.3f)\n", what, ok ? "ok" : "FAIL", a, b);
    fails += !ok;
}
static tk_layer_t layer(int eng, int src, int pos, int len, int dens, int sprd, int intv, int ptch, int det, int width,
                        int level, int cut, int atk, int rel, int rev, int dly, int evo)
{
    tk_layer_t l = {eng, src, pos, len, dens, sprd, intv, ptch, det, width, level, cut, atk, rel, rev, dly, evo, 0};
    return l;
}
static void globals(int evo, int time, int cho, int dt, int dfb, int rs, int rd)
{
    T.p.vx = T.p.vy = 64; T.p.evo = evo; T.p.evo_time = time; T.p.cho = cho; T.p.dly_time = dt; T.p.dly_fb = dfb;
    T.p.rev_size = rs; T.p.rev_damp = rd; T.p.master = 100;
}
/* de bronnen zoals op het apparaat: de ingebouwde, en daarachter die uit bank/extra.tkb (tools/tekstuur_samples.py) */
static tk_src_t SRC[TK_NSOURCES + 8];
static uint32_t nsrc;
static void sources(void)
{
    static uint8_t bank[300000];
    static char names[8][9];
    FILE *f = fopen("bank/extra.tkb", "rb");
    uint32_t i, n = f ? (uint32_t)fread(bank, 1, sizeof bank, f) : 0;
    if (f) fclose(f);
    for (nsrc = 0; nsrc < TK_NSOURCES; nsrc++) SRC[nsrc] = TK_SOURCES[nsrc];
    if (n < 16 || memcmp(bank, "TKB1", 4)) return;
    for (i = 0; i < bank[12] && i < 8; i++) {
        const uint8_t *e = bank + 16 + 24 * i;
        uint32_t off, len, rate;
        memcpy(names[i], e, 8);
        memcpy(&off, e + 8, 4); memcpy(&len, e + 12, 4); memcpy(&rate, e + 16, 4);
        SRC[nsrc].d = bank + off; SRC[nsrc].len = len; SRC[nsrc].rate = rate; SRC[nsrc].root = e[20]; SRC[nsrc].name = names[i];
        nsrc++;
    }
}
static void fresh(void)
{
    if (!nsrc) sources();
    tk_init(&T, SRC, nsrc, dly_mem, rv_mem, res_mem);
}

/* de presets van de firmware */
static void preset(int k)
{
    fresh();
    T.p = TK_PRESETS[k % TK_NPRESETS].p;
    if (TK_LEVELS[k % TK_NPRESETS])
        T.p.master = TK_LEVELS[k % TK_NPRESETS];
}

static void wav(const char *path, const int16_t *x, uint32_t frames)
{
    FILE *f = fopen(path, "wb");
    uint32_t n = frames * 4u, h[11] = {0x46464952, 36 + n, 0x45564157, 0x20746d66, 16, 0x00020001, TK_FS, TK_FS * 4, 0x00100004,
                                       0x61746164, n};
    if (!f) { printf("kan %s niet schrijven\n", path); return; }
    fwrite(h, 4, 11, f);
    fwrite(x, 2, frames * 2u, f);
    fclose(f);
}
/* secs renderen met een akkoord dat hold seconden vastgehouden wordt; -> piek, RMS (dBFS), stereobreedte (zij/midden) */
static void play(const int *notes, int nn, double secs, double hold, int16_t *out, double *peak, double *rms, double *side)
{
    static int32_t b[2 * TK_BLK];
    uint32_t blocks = (uint32_t)(secs * TK_FS / TK_BLK), off = (uint32_t)(hold * TK_FS / TK_BLK), k, i;
    double pk = 0, ss = 0, sm = 0, sd = 0;
    for (i = 0; i < (uint32_t)nn; i++) tk_note_on(&T, notes[i], 100);
    for (k = 0; k < blocks; k++) {
        if (k == off) tk_all_off(&T);
        tk_render(&T, b, TK_BLK);
        for (i = 0; i < 2 * TK_BLK; i++) {
            double v = (b[i] >> 8) / 32768.0;
            if (out) out[k * 2 * TK_BLK + i] = (int16_t)(b[i] >> 8);
            if (fabs(v) > pk) pk = fabs(v);
            ss += v * v;
        }
        for (i = 0; i < TK_BLK; i++) {
            double l = (b[2 * i] >> 8) / 32768.0, r = (b[2 * i + 1] >> 8) / 32768.0;
            sm += (l + r) * (l + r); sd += (l - r) * (l - r);
        }
    }
    if (peak) *peak = 20 * log10(pk + 1e-9);
    if (rms) *rms = 10 * log10(ss / (blocks * 2.0 * TK_BLK) + 1e-12);
    if (side) *side = 10 * log10((sd + 1e-12) / (sm + 1e-12));
}

int main(int argc, char **argv)
{
    static const int CH[4][3] = {{50, 57, 62}, {60, 67, 72}, {41, 48, 53}, {48, 55, 64}};
    static int16_t buf[2 * TK_FS * 31];
    int k;
    double pk, rms, side;
    if (argc > 1 && !strcmp(argv[1], "levels")) {     /* het volume van elke preset: RMS -18 dBFS bij een akkoord */
        printf("/* gemaakt door build/tk_host levels, niet bewerken: het volume (master) van elke ingebouwde preset */\n");
        printf("static const uint8_t TK_LEVELS[] = {");
        for (k = 0; k < (int)TK_NPRESETS; k++) {
            int it, m = 100;
            for (it = 0; it < 4; it++) {
                fresh();
                T.p = TK_PRESETS[k].p;
                T.p.master = (uint8_t)m;
                play(CH[k % 4], 3, 24, 18, 0, &pk, &rms, 0);
                m = (int)(m * pow(10, (-18.0 - rms) / 20) + 0.5);
                if (m > 127) m = 127;
                if (m < 20) m = 20;
            }
            fprintf(stderr, "%-14s master %3d  RMS %6.1f  piek %6.1f%s\n", TK_PRESETS[k].name, m, rms, pk, m == 127 ? "  (zachter dan de rest)" : "");
            printf("%s%d,", k % 16 ? " " : "\n    ", m);
        }
        printf("\n};\n");
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "demo")) {
        for (k = 0; k < (int)TK_NPRESETS; k++) {
            char path[512], nm[32];
            int i;
            preset(k);
            play(CH[k % 4], 3, 30, 22, buf, &pk, &rms, &side);
            for (i = 0; TK_PRESETS[k].name[i] && i < 31; i++)
                nm[i] = TK_PRESETS[k].name[i] == ' ' ? '_' : (char)(TK_PRESETS[k].name[i] + 32 * (TK_PRESETS[k].name[i] >= 'A' && TK_PRESETS[k].name[i] <= 'Z'));
            nm[i] = 0;
            snprintf(path, sizeof path, "%s/%02d_%s.wav", argv[2], k + 1, nm);
            wav(path, buf, (uint32_t)(30 * TK_FS / TK_BLK) * TK_BLK);
            printf("%-12s piek %6.1f dBFS  RMS %6.1f dBFS  zij/midden %6.1f dB  doorgangen max %u\n", TK_PRESETS[k].name, pk, rms, side, T.npass_peak);
        }
        return 0;
    }
    /* stilte zonder noten, en stil na het loslaten */
    preset(11);
    play(CH[3], 0, 2, 0, 0, &pk, 0, 0);
    check("geen noten: stil (piek dBFS)", pk < -100, pk, 0);
    preset(11);
    play(CH[3], 3, 20, 8, 0, &pk, &rms, &side);
    check("akkoord, vier lagen: niet tegen de begrenzer (piek dBFS)", pk < -1.0 && pk > -20, pk, rms);
    check("stereo: zij/midden boven -15 dB", side > -15, side, 0);
    check("doorgangen binnen de pool", T.npass_peak <= TK_NP, T.npass_peak, TK_NP);
    T.p.rev_size = 0; T.p.dly_fb = 0;
    for (k = 0; k < 4; k++) { T.p.l[k].rev = 0; T.p.l[k].dly = 0; }
    play(CH[3], 0, 30, 0, 0, 0, 0, 0);
    play(CH[3], 0, 1, 0, 0, &pk, 0, 0);
    check("na loslaten en uitklinken: stil (piek dBFS)", pk < -80, pk, tk_passes(&T));
    /* klikken: de grootste sprong tussen twee samples in een aangehouden lus, tegen de piek */
    {
        static int32_t b[2 * TK_BLK];
        int32_t prev = 0, jump = 0, peak = 0, i;
        fresh();
        T.p.l[0] = layer(TK_LOOP, 1, 50, 60, 0, 40, TK_UNI, 0, 30, 0, 110, 127, 30, 60, 0, 0, 0);
        globals(0, 40, 0, 60, 0, 0, 0);
        tk_note_on(&T, 38, 100);
        for (k = 0; k < 30 * TK_FS / TK_BLK; k++) {
            tk_render(&T, b, TK_BLK);
            for (i = 0; i < TK_BLK; i++) {
                int32_t v = b[2 * i] >> 8;
                if (k > 200) {
                    if (abs(v - prev) > jump) jump = abs(v - prev);
                    if (abs(v) > peak) peak = abs(v);
                }
                prev = v;
            }
        }
        check("lus zonder klik: grootste sprong / piek onder 0,35", peak > 500 && (double)jump / peak < 0.35, (double)jump / peak, peak);
    }
    /* de nieuwe engines en effecten */
    {
        static int32_t b[2 * TK_BLK];
        static double mono[TK_FS * 4];
        int n = 0, i, lag, bl = 0;
        double best = 0, cents;
        /* RESON: de snaren klinken op de gespeelde noot (A3 = 220 Hz), en klinken na */
        fresh();
        T.p.l[0] = layer(TK_RESON, S_NOI, 50, 110, 90, 60, TK_UNI, 0, 0, 0, 110, 127, 0, 20, 0, 0, 0);
        globals(0, 40, 0, 60, 0, 0, 0);
        tk_note_on(&T, 57, 100);
        for (k = 0; k < 4 * TK_FS / TK_BLK; k++) {
            tk_render(&T, b, TK_BLK);
            for (i = 0; i < TK_BLK; i++) mono[n++] = (b[2 * i] >> 8) / 32768.0;
        }
        for (lag = 60; lag < 160; lag++) {
            double c = 0;
            for (i = TK_FS; i < 3 * TK_FS; i++) c += mono[i] * mono[i + lag];
            if (c > best) { best = c; bl = lag; }
        }
        {   /* parabool door de top voor een fijnere periode */
            double c[3] = {0, 0, 0};
            int j;
            for (j = 0; j < 3; j++) for (i = TK_FS; i < 3 * TK_FS; i++) c[j] += mono[i] * mono[i + bl - 1 + j];
            cents = 1200 * log2((double)TK_FS / (bl + 0.5 * (c[0] - c[2]) / (c[0] - 2 * c[1] + c[2])) / 220.0);
        }
        check("RESON: de snaar staat op de noot (cent, binnen 8)", fabs(cents) < 8, cents, bl);
        play(CH[3], 0, 1, 99, 0, &pk, &rms, 0);
        check("RESON: niveau bij aanhouden (RMS dBFS tussen -30 en -8)", rms > -30 && rms < -8, rms, pk);
        tk_all_off(&T);
        play(CH[3], 0, 1.5, 99, 0, 0, 0, 0);
        play(CH[3], 0, 0.5, 99, 0, &pk, 0, 0);
        check("RESON: klinkt na na het loslaten (piek na 1,5 s boven -50)", pk > -50, pk, 0);
        play(CH[3], 0, 40, 99, 0, 0, 0, 0);
        play(CH[3], 0, 1, 99, 0, &pk, 0, 0);
        check("RESON: daarna stil (piek dBFS)", pk < -80, pk, 0);
        /* STRETCH: stilstaand klinkt het door, met weinig doorgangen */
        fresh();
        T.p.l[0] = layer(TK_STRETCH, S_STR, 50, 80, 0, 30, TK_UNI, 0, 30, 100, 110, 127, 20, 60, 0, 0, 0);
        globals(0, 40, 0, 60, 0, 0, 0);
        tk_note_on(&T, 60, 100);
        play(CH[3], 0, 2, 99, 0, 0, 0, 0);
        T.npass_peak = 0;
        play(CH[3], 0, 6, 99, 0, &pk, &rms, 0);
        check("STRETCH: klinkt door (RMS dBFS tussen -30 en -10)", rms > -30 && rms < -10, rms, pk);
        check("STRETCH: hooguit 6 doorgangen per noot", T.npass_peak <= 6, T.npass_peak, 0);
        /* bevriezen: de galm houdt vast na het loslaten, en loopt weer leeg na het ontdooien */
        preset(0);
        play(CH[0], 3, 8, 99, 0, 0, &rms, 0);
        T.freeze = 1;
        play(CH[0], 0, 1, 99, 0, 0, 0, 0);
        tk_all_off(&T);
        play(CH[0], 0, 30, 99, 0, 0, 0, 0);
        { double r2; play(CH[0], 0, 2, 99, 0, &pk, &r2, 0);
          check("FREEZE: na 30 s nog te horen (RMS dBFS boven -45)", r2 > -45 && pk < -1, r2, pk); }
        T.freeze = 0;
        play(CH[0], 0, 40, 99, 0, 0, 0, 0);
        play(CH[0], 0, 1, 99, 0, &pk, 0, 0);
        check("FREEZE uit: weer stil (piek dBFS)", pk < -80, pk, 0);
        /* shimmer en resonantie op hun hoogst: het loopt niet op en dooft uit */
        preset(0);
        T.p.shim = 127; T.p.rev_size = 127;
        for (k = 0; k < 4; k++) { T.p.l[k].res = 127; T.p.l[k].rev = 127; }
        play(CH[0], 3, 40, 99, 0, &pk, &rms, 0);
        check("shimmer, galm en resonantie vol: binnen de perken (RMS dBFS onder -9)", rms < -9, rms, pk);
        tk_all_off(&T);
        play(CH[0], 0, 90, 99, 0, 0, 0, 0);
        play(CH[0], 0, 1, 99, 0, &pk, 0, 0);
        check("..en dooft uit (piek dBFS)", pk < -70, pk, 0);
        /* shimmer doet iets: meer hoog in de galm dan zonder */
        {
            double e[2];
            int w;
            for (w = 0; w < 2; w++) {
                double hp = 0, prev = 0, acc = 0;
                fresh();
                T.p.l[0] = layer(TK_LOOP, S_CEL, 50, 90, 0, 40, TK_UNI, 0, 30, 100, 110, 70, 30, 60, 127, 0, 0);
                globals(0, 40, 0, 60, 0, 110, 10);
                T.p.shim = w ? 110 : 0;
                tk_note_on(&T, 48, 100);
                for (k = 0; k < 12 * TK_FS / TK_BLK; k++) {
                    tk_render(&T, b, TK_BLK);
                    for (i = 0; i < TK_BLK; i++) {
                        double v = (b[2 * i] >> 8) / 32768.0;
                        hp = 0.9 * (hp + v - prev); prev = v;      /* hoogdoorlaat rond 350 Hz */
                        if (k > 6 * TK_FS / TK_BLK) acc += hp * hp;
                    }
                }
                e[w] = 10 * log10(acc + 1e-12);
            }
            check("shimmer: meer hoog in de galm (dB erbij, minstens 2)", e[1] - e[0] > 2, e[1] - e[0], e[0]);
        }
    }
    /* rekentijd */
    {
        clock_t c0;
        double t;
        preset(11);
        play(CH[3], 3, 2, 99, 0, 0, 0, 0);
        c0 = clock();
        play(CH[3], 0, 60, 99, 0, 0, 0, 0);
        t = (double)(clock() - c0) / CLOCKS_PER_SEC;
        printf("rekentijd: 60 s geluid in %.0f ms op deze Mac (%.0f x realtime), doorgangen max %u\n", t * 1000, 60 / t, T.npass_peak);
    }
    printf(fails ? "MISLUKT\n" : "alles ok\n");
    return fails != 0;
}
