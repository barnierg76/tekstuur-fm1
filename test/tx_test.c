/* Host tests and renders of the TEXTURE engine (floating point only here, never in the engine). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/tx_engine.h"

#ifndef TX_BLOCK
#define TX_BLOCK 64
#endif
static int fails;
static void check(const char *name, int ok, const char *fmt, double a, double b)
{
    printf("%s %s: ", ok ? "PASS" : "FAIL", name);
    printf(fmt, a, b);
    printf("\n");
    fails += !ok;
}

static int16_t *load_raw(const char *path, uint32_t *n)
{
    FILE *f = fopen(path, "rb");
    long sz;
    int16_t *d;
    if (!f) { *n = 0; return 0; }
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    d = malloc((size_t)sz);
    *n = (uint32_t)(fread(d, 2, (size_t)sz / 2, f));
    fclose(f);
    return d;
}

/* render secs seconds of the part into a float buffer (16-bit scale) */
static float *render(tx_part_t *P, double secs, uint32_t *frames)
{
    uint32_t n = (uint32_t)(secs * TX_RATE), i, k;
    float *o = calloc(n, sizeof *o);
    for (i = 0; i + TX_BLOCK <= n; i += TX_BLOCK) {
        int32_t b[TX_BLOCK] = {0};
        tx_render(P, b, TX_BLOCK);
        for (k = 0; k < TX_BLOCK; k++)
            o[i + k] = (float)b[k];
    }
    *frames = n;
    return o;
}

static void wav_write(const char *path, const float *l, const float *r, uint32_t n, double gain)
{
    FILE *f = fopen(path, "wb");
    uint32_t i, bytes = n * 4u, v;
    uint16_t s;
    fwrite("RIFF", 1, 4, f); v = 36u + bytes; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    v = 16; fwrite(&v, 4, 1, f); s = 1; fwrite(&s, 2, 1, f); s = 2; fwrite(&s, 2, 1, f);
    v = TX_RATE; fwrite(&v, 4, 1, f); v = TX_RATE * 4u; fwrite(&v, 4, 1, f); s = 4; fwrite(&s, 2, 1, f);
    s = 16; fwrite(&s, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    for (i = 0; i < n; i++) {
        double a = l[i] * gain, b = (r ? r[i] : l[i]) * gain;
        int16_t x = (int16_t)(a > 32767 ? 32767 : a < -32768 ? -32768 : a);
        int16_t y = (int16_t)(b > 32767 ? 32767 : b < -32768 ? -32768 : b);
        fwrite(&x, 2, 1, f); fwrite(&y, 2, 1, f);
    }
    fclose(f);
}

static double rms(const float *x, uint32_t a, uint32_t b)
{
    double s = 0; uint32_t i;
    for (i = a; i < b; i++) s += (double)x[i] * x[i];
    return sqrt(s / (b - a));
}

static void tests(void)
{
    enum { N = 44100 };
    static int16_t dc[N], sine[N], noise[N];
    tx_part_t P;
    float *o;
    uint32_t n, i, seed = 1;
    for (i = 0; i < N; i++) {
        dc[i] = 10000;
        sine[i] = (int16_t)(12000.0 * sin(2 * M_PI * 261.6256 * i / TX_RATE));
        seed = seed * 1664525u + 1013904223u;
        noise[i] = (int16_t)((int32_t)(seed >> 16) - 32768) / 3;
    }
    /* 1. the seam: a constant source through one voice must come out constant once the heads are in */
    {
        double mn = 1e9, mx = -1e9, step = 0;
        tx_init(&P, dc, N, 60);
        P.par[TX_LEN] = 60; P.par[TX_DRFT] = 127; P.par[TX_EVOL] = 127;
        tx_note_on(&P, 0, 60);
        o = render(&P, 20.0, &n);
        for (i = 4 * TX_RATE; i < n - TX_BLOCK; i++) {
            if (o[i] < mn) mn = o[i];
            if (o[i] > mx) mx = o[i];
            if (fabs(o[i] - o[i - 1]) > step) step = fabs(o[i] - o[i - 1]);
        }
        check("seam: constant source stays constant", (mx - mn) / mx < 0.01, "ripple %.3f %%, largest step %.2f", 100 * (mx - mn) / mx, step);
        free(o);
    }
    /* 2. pitch: a C4 sine as source, played an octave up, must sound at twice the frequency */
    {
        uint32_t zc = 0;
        double f;
        tx_init(&P, sine, N, 60);
        P.par[TX_DRFT] = 0; P.par[TX_LEN] = 100;
        tx_note_on(&P, 0, 72);
        o = render(&P, 1.0, &n);
        /* the first head alone, inside one pass */
        for (i = 4000; i < 10000; i++)
            zc += o[i - 1] < 0 && o[i] >= 0;
        f = zc * (double)TX_RATE / 6000.0;
        check("pitch: octave up", fabs(f / 523.2511 - 1) < 0.01, "%.1f Hz, wanted %.1f", f, 523.2511);
        free(o);
    }
    /* 3. level: noise source, the level in 200 ms windows stays within +-2 dB after the swell */
    {
        double lo = 1e9, hi = 0;
        tx_init(&P, noise, N, 60);
        P.par[TX_LEN] = 70; P.par[TX_DRFT] = 60; P.par[TX_EVOL] = 100;
        tx_note_on(&P, 0, 60);
        o = render(&P, 30.0, &n);
        for (i = 4 * TX_RATE; i + 8820 < n; i += 8820) {
            double r = rms(o, i, i + 8820);
            if (r < lo) lo = r;
            if (r > hi) hi = r;
        }
        check("level: steady on noise", 20 * log10(hi / lo) < 4.0, "spread %.2f dB (limit %.1f)", 20 * log10(hi / lo), 4.0);
        free(o);
    }
    /* 4. EVOL moves the read position on its own, EVOL 0 leaves it */
    {
        uint32_t lo = 65535, hi = 0, p0, p1;
        tx_init(&P, noise, N, 60);
        P.par[TX_EVOL] = 0;
        tx_note_on(&P, 0, 60);
        p0 = tx_position(&P);
        o = render(&P, 10.0, &n); free(o);
        p1 = tx_position(&P);
        P.par[TX_EVOL] = 127;
        for (i = 0; i < 80; i++) {
            uint32_t p;
            o = render(&P, 1.0, &n); free(o);
            p = tx_position(&P);
            if (p < lo) lo = p;
            if (p > hi) hi = p;
        }
        check("evolve: off stays, on travels", p0 == p1 && hi - lo > 30000, "moved %.0f of 65535 (off: %.0f)", (double)(hi - lo), (double)(p1 - p0));
    }
    /* 5. three voices fit the pool: every voice keeps sounding, the pool never overflows */
    {
        uint32_t used = 0, peak = 0, k, quiet = 0;
        tx_init(&P, noise, N, 60);
        P.par[TX_LEN] = 40;
        tx_note_on(&P, 0, 60); tx_note_on(&P, 1, 64); tx_note_on(&P, 2, 67);
        for (i = 0; i < 20 * TX_RATE / TX_BLOCK; i++) {
            int32_t b[TX_BLOCK] = {0};
            uint32_t own[TX_POLY] = {0};
            tx_render(&P, b, TX_BLOCK);
            for (k = 0, used = 0; k < TX_NP; k++)
                if (P.p[k].owner) { used++; own[P.p[k].owner - 1]++; }
            if (used > peak) peak = used;
            if (i > 2 * TX_RATE / TX_BLOCK)
                for (k = 0; k < TX_POLY; k++)
                    quiet += !own[k];
        }
        check("pool: three voices", peak <= TX_NP && quiet == 0, "peak %.0f of 12 passes, blocks with a silent voice %.0f", (double)peak, (double)quiet);
    }
    /* 6. cost: passes running at once, one voice and three (GRAIN's bound is 12 grains) */
    {
        double sum1 = 0, sum3 = 0; uint32_t k, blocks = 20 * TX_RATE / TX_BLOCK;
        tx_init(&P, noise, N, 60);
        tx_note_on(&P, 0, 60);
        for (i = 0; i < blocks; i++) {
            int32_t b[TX_BLOCK] = {0};
            tx_render(&P, b, TX_BLOCK);
            for (k = 0; k < TX_NP; k++) sum1 += P.p[k].owner != 0;
        }
        tx_note_on(&P, 1, 64); tx_note_on(&P, 2, 67);
        for (i = 0; i < blocks; i++) {
            int32_t b[TX_BLOCK] = {0};
            tx_render(&P, b, TX_BLOCK);
            for (k = 0; k < TX_NP; k++) sum3 += P.p[k].owner != 0;
        }
        check("cost: passes at once", sum3 / blocks <= 12.0, "one voice %.2f, three voices %.2f (GRAIN: up to 12)", sum1 / blocks, sum3 / blocks);
    }
}

/* a demo: a chord held with a slow attack and release; stereo by rendering two parts with different seeds */
static void demo(const char *raw, int root, const char *out, int len, int intv, int drft, int evol, int tone, int pos,
                 const int *notes, int nnotes, double secs)
{
    uint32_t n, i, k, frames = (uint32_t)(secs * TX_RATE);
    int16_t *d = load_raw(raw, &n);
    tx_part_t L, R;
    float *l, *r;
    double peak = 1;
    if (!d) { printf("no %s\n", raw); return; }
    tx_init(&L, d, n, root); tx_init(&R, d, n, root);
    R.rng = 0x1234567u; R.evo1 = 0x40000000u; R.evo2 = 0x90000000u;
    for (k = 0; k < 2; k++) {
        tx_part_t *P = k ? &R : &L;
        P->par[TX_LEN] = (int16_t)len; P->par[TX_INTV] = (int16_t)intv; P->par[TX_DRFT] = (int16_t)drft;
        P->par[TX_EVOL] = (int16_t)evol; P->par[TX_TONE] = (int16_t)tone; P->par[TX_POS] = (int16_t)pos;
        for (i = 0; i < (uint32_t)nnotes; i++)
            tx_note_on(P, i, (uint32_t)notes[i]);
    }
    l = render(&L, secs, &frames); r = render(&R, secs, &frames);
    for (i = 0; i < frames; i++) {
        double t = (double)i / TX_RATE, e = t < 1.5 ? t / 1.5 : t > secs - 3.0 ? (secs - t) / 3.0 : 1.0;
        e = e < 0 ? 0 : e * e;
        l[i] *= (float)e; r[i] *= (float)e;
        if (fabs(l[i]) > peak) peak = fabs(l[i]);
        if (fabs(r[i]) > peak) peak = fabs(r[i]);
    }
    wav_write(out, l, r, frames, 26000.0 / peak);
    printf("wrote %s (%.0f s)\n", out, secs);
    free(l); free(r); free(d);
}

/* the source as it is, looped: what the sample sounds like without the engine */
static void dry(const char *raw, const char *out, double secs)
{
    uint32_t n, i, frames = (uint32_t)(secs * TX_RATE);
    int16_t *d = load_raw(raw, &n);
    float *l;
    if (!d) return;
    l = calloc(frames, sizeof *l);
    for (i = 0; i < frames; i++)
        l[i] = d[i % n];
    wav_write(out, l, 0, frames, 0.8);
    free(l); free(d);
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "demo")) {
        int croot = argc > 2 ? atoi(argv[2]) : 38, kroot = argc > 3 ? atoi(argv[3]) : 58;
        int c1[] = {croot}, c3[] = {croot, croot + 7, croot + 12}, k3[] = {kroot, kroot + 7, kroot + 12}, k1[] = {kroot - 5};
        dry("build/cello.raw", "build/0_cello_bron.wav", 6);
        dry("build/kane.raw", "build/0_bel_bron.wav", 6);
        demo("build/cello.raw", croot, "build/1_cello_lussen.wav", 86, TX_UNI, 40, 0, 110, 55, c1, 1, 30);
        demo("build/cello.raw", croot, "build/2_cello_octaven_evolve.wav", 92, TX_OCT, 50, 110, 100, 50, c1, 1, 45);
        demo("build/cello.raw", croot, "build/3_cello_akkoord_kwint.wav", 80, TX_FIFTH, 45, 80, 90, 50, c3, 3, 40);
        demo("build/kane.raw", kroot, "build/4_bel_akkoord_octaaf_kwint.wav", 96, TX_OCT5, 35, 100, 127, 30, k3, 3, 40);
        demo("build/kane.raw", kroot, "build/5_bel_sub_donker.wav", 100, TX_SUB, 60, 90, 60, 40, k1, 1, 40);
        return 0;
    }
    tests();
    printf(fails ? "\n%d FAILED\n" : "\nALL OK\n", fails);
    return fails != 0;
}
