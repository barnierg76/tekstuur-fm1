/* Tekstuur voor de FM-1: de klankmotor. Copyright (C) 2026 BARNIR. Alle rechten voorbehouden.
 *
 * Vier lagen (A..D) die elk een eigen bron bespelen met een eigen engine, gemengd met een vectorpunt, door een
 * eigen chorus, delay en galm, in stereo. Alleen hele getallen (de FM-1 heeft geen drijvende komma).
 *
 * Alles wat klinkt is een "doorgang": een stuk van een bron, afgespeeld op een snelheid, onder een venster dat
 * op- en afloopt, op een plek in het stereobeeld. De engines verschillen alleen in hoe ze doorgangen starten:
 *   LOOP   drie koppen per noot met elk een eigen luslengte (256 : 331 : 199), elke doorgang loopt over in de
 *          volgende; de koppen kunnen een interval uit elkaar liggen. Lange, rustige, steeds verschuivende klank.
 *   GRAIN  een wolk korte korrels rond een plek in de bron, met spreiding in plek, toonhoogte en panorama.
 *   STRETCH korrels die dicht op elkaar liggen en heel langzaam door de bron schuiven (of stilstaan): de klank uitgerekt.
 *   RESON  een korrelwolk die drie gestemde snaren aanslaat (kamfilters op de gespeelde noot en het interval): de bron
 *          geeft de kleur, de snaren de toon; ze klinken uit na het loslaten.
 * Bronnen: mono, 12 bits per sample (3 bytes per 2 samples), elk met een eigen samplefrequentie en grondtoon. */
#pragma once
#include <stdint.h>

#define TK_FS 22050                    /* de motor rekent op de samplefrequentie van de bronnen; de app maakt er 44,1 kHz
                                        * van (app.c). Boven 11 kHz zit in de bronnen niets, en het halveert de rekentijd */
#define TK_BLK 32                      /* samples per regelblok */
#define TK_NL 4                        /* lagen */
#define TK_NV 5                        /* noten tegelijk */
#define TK_NP 64                       /* doorgangen tegelijk (alle lagen en noten samen) */
#define TK_HEADS 3
#define TK_DLY_MAX 16384               /* delay: 0,74 s */

typedef struct {
    const uint8_t *d;                  /* 12 bits per sample, paren in 3 bytes */
    uint32_t len;                      /* samples */
    uint32_t rate;                     /* Hz */
    uint8_t root;                      /* MIDI-noot van de opname */
    const char *name;
} tk_src_t;

enum { TK_OFF, TK_LOOP, TK_GRAIN, TK_STRETCH, TK_RESON, TK_NENG };
enum { TK_UNI, TK_OCT, TK_FIFTH, TK_OCT5, TK_SUB, TK_NINTV };

typedef struct {                       /* een laag; alles 0..127 tenzij anders vermeld */
    uint8_t eng, src;
    uint8_t pos;                       /* plek in de bron */
    uint8_t len;                       /* LOOP: luslengte; GRAIN, STRETCH: korrelgrootte; RESON: hoe lang de snaren naklinken */
    uint8_t dens;                      /* GRAIN, RESON: korrels per seconde; STRETCH: hoe snel de plek door de bron schuift */
    uint8_t sprd;                      /* GRAIN: spreiding van de plek; LOOP: verschil tussen de koppen */
    uint8_t intv;                      /* TK_UNI.. */
    int8_t ptch;                       /* halve tonen, -24..24 */
    uint8_t detune;                    /* zweving tussen koppen of korrels */
    uint8_t width;                     /* stereobreedte */
    uint8_t level;
    uint8_t cut;                       /* laagdoorlaat */
    uint8_t atk, rel;
    uint8_t rev, dly;                  /* sends */
    uint8_t evo;                       /* hoeveel Evolve deze laag beweegt */
    uint8_t res;                       /* resonantie van het filter */
} tk_layer_t;

typedef struct {
    tk_layer_t l[TK_NL];
    uint8_t vx, vy;                    /* vectorpunt: 64, 64 = alle lagen; een hoek = alleen die laag (A linksboven,
                                        * B rechtsboven, C linksonder, D rechtsonder) */
    uint8_t evo, evo_time;             /* Evolve: diepte, duur van de reis */
    uint8_t cho;                       /* chorus */
    uint8_t dly_time, dly_fb;
    uint8_t rev_size, rev_damp;
    uint8_t master;
    uint8_t shim;                      /* shimmer: de galm een octaaf hoger terug de galm in */
} tk_patch_t;

typedef struct {
    uint8_t on, layer, voice, kill;
    const tk_src_t *s;
    uint32_t pos, rate;                /* Q12 samples van de bron */
    uint32_t age, dur, ramp;           /* uitvoersamples */
    int32_t gl, gr;                    /* Q15: plek in het beeld, aanslag, laagniveau */
    int32_t g0;                        /* Q15: venster x omhullende aan het begin van het blok */
} tk_pass_t;

typedef struct {
    uint8_t on, held, note, vel;
    uint32_t age;
    int32_t env[TK_NL];                /* Q15 */
    int32_t cnt[TK_NL][TK_HEADS];      /* LOOP: samples tot de volgende doorgang van die kop */
    uint32_t acc[TK_NL];               /* GRAIN: teller */
    uint32_t scan[TK_NL];              /* STRETCH: hoe ver de plek verschoven is, Q8 */
    uint8_t ru[TK_NL];                 /* RESON: de snaren van deze noot op deze laag (255 = geen) */
} tk_voice_t;

#define TK_NRES 8                      /* stellen snaren tegelijk */
#define TK_RES_LEN 1024                /* samples per snaar: de laagste toon is 21,5 Hz */
typedef struct {                       /* drie snaren voor een noot op een laag */
    uint8_t on, layer, voice;
    uint32_t wi, peak;
    uint32_t per[TK_HEADS];            /* periode, Q8 samples */
    int32_t fb[TK_HEADS], ing[TK_HEADS];   /* Q15: terugkoppeling, ingangsniveau */
    int32_t lp[TK_HEADS], dc[TK_HEADS];
    int32_t gl[TK_HEADS], gr[TK_HEADS];    /* Q15: plek in het beeld */
    int32_t *d;                        /* TK_HEADS x TK_RES_LEN, 256 x de waarde: de staart dooft glad uit */
} tk_res_t;

typedef struct {
    tk_patch_t p;
    const tk_src_t *src;
    uint32_t nsrc;
    tk_voice_t v[TK_NV];
    tk_pass_t ps[TK_NP];
    uint32_t rng, e1, e2, lfo;
    int32_t vg[TK_NL];                 /* Q15: vectorniveau per laag, gegleden */
    int32_t fl[TK_NL][4];              /* laagfilter: laag en band, L en R (16 x de waarde) */
    int32_t cutq[TK_NL], resq[TK_NL], openq[TK_NL];   /* Q12: frequentie, demping, hoe ver het filter openstaat */
    tk_res_t ru[TK_NRES];
    int16_t *shm;                      /* shimmer: TK_SHM samples van de galm */
    uint32_t shw;
    int32_t shlp[2], shdc, frz;              /* frz Q15: hoe bevroren de galm is */
    uint8_t freeze;                    /* de app zet dit: de galm houdt vast wat erin zit */
    /* effecten */
    int16_t cho_l[1024], cho_r[1024];
    int16_t *dly_l, *dly_r;            /* TK_DLY_MAX elk */
    int16_t *rv;                       /* galm: TK_RV_MEM samples */
    uint32_t wi, dwi;
    uint16_t ri[7];                    /* galm: de plek in elk van de zeven lijnen */
    uint8_t vx_now, vy_now;            /* het vectorpunt zoals Evolve het nu verplaatst heeft (voor het scherm) */
    uint8_t np_max;                    /* zoveel doorgangen tegelijk (<= TK_NP); de app verlaagt het bij overbelasting */
    int32_t dlp_l, dlp_r, rlp[4], dc_l, dc_r;
    uint32_t npass_peak;
} tk_t;

#define TK_SHM 2048
#define TK_RV_MEM (8192 + TK_SHM)              /* samples galmgeheugen */

void tk_init(tk_t *t, const tk_src_t *src, uint32_t nsrc, int16_t *dly_mem /* 2 * TK_DLY_MAX */, int16_t *rv_mem /* TK_RV_MEM */,
             int32_t *res_mem /* TK_NRES * TK_HEADS * TK_RES_LEN */);
void tk_note_on(tk_t *t, uint32_t note, uint32_t vel);
void tk_note_off(tk_t *t, uint32_t note);
void tk_all_off(tk_t *t);
void tk_render(tk_t *t, int32_t *lr, uint32_t n);   /* n stereoframes (veelvoud van TK_BLK), 24 bits */
uint32_t tk_passes(const tk_t *t);
uint32_t tk_src_peak(const tk_src_t *s, uint32_t a, uint32_t b);
