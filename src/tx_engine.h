/* TEXTURE for the M-VAVE FM-1: Tekstuur's Multi-Loop, interval stacking and Evolve as a small
 * fixed-point engine. Integer arithmetic only (the FM-1's engines run in Q15 / Q16).
 *
 * A voice plays up to TX_HEADS loop heads over one short sample. Each head repeats a stretch of
 * the sample with its own length (co-prime ratios, so the heads never line up again within
 * minutes), its own interval (unison, octaves, fifths) and a little drift. A pass fades in and
 * out with a sin^2 ramp and the next pass starts one fade before the end: sin^2 + cos^2 = 1, so
 * a head has no seam. EVOL moves the read position slowly through the sample on its own. */
#ifndef TX_ENGINE_H
#define TX_ENGINE_H
#include <stdint.h>

#define TX_POLY 3      /* voices per part */
#define TX_HEADS 3     /* loop heads per voice (fewer when more voices sound) */
#define TX_NP 12       /* passes per part: the same pool size as Felucca's GRAIN */
#define TX_RATE 44100

enum { TX_POS, TX_LEN, TX_INTV, TX_PTCH, TX_DRFT, TX_EVOL, TX_TONE, TX_NPARAM };
enum { TX_UNI, TX_OCT, TX_FIFTH, TX_OCT5, TX_SUB, TX_NINTV };

typedef struct {
    const int16_t *data;   /* mono source */
    uint32_t n;            /* samples */
    int32_t root16;        /* its pitch: semitones * 256 (MIDI note 60 = 60 << 8) */
} tx_zone_t;

typedef struct {
    uint32_t pos;          /* source index of sample a */
    int32_t frac;          /* Q16 between a and b */
    uint32_t step;         /* Q16 source samples per output sample */
    uint32_t len, left;    /* output samples: whole pass, still to go */
    uint32_t fade;         /* output samples of each ramp */
    int32_t gain;          /* Q15 */
    uint8_t owner;         /* voice + 1, 0 = free */
    uint8_t head;
} tx_pass_t;

typedef struct {
    uint8_t active, note;
    int32_t pitch16;                /* note * 256 + bend */
    int32_t next[TX_HEADS];         /* output samples until head h starts its next pass */
    int32_t lp;                     /* tone filter state */
} tx_voice_t;

typedef struct {
    tx_zone_t zone;
    tx_pass_t p[TX_NP];
    tx_voice_t v[TX_POLY];
    int16_t par[TX_NPARAM];         /* 0..127 (PTCH: -24..24, INTV: 0..TX_NINTV-1) */
    uint32_t evo1, evo2;            /* the two slow phases of EVOL */
    uint32_t rng;
} tx_part_t;

void tx_init(tx_part_t *P, const int16_t *data, uint32_t n, int32_t root_note);
void tx_note_on(tx_part_t *P, uint32_t voice, uint32_t note);
void tx_note_off(tx_part_t *P, uint32_t voice);
/* adds n mono samples (16-bit scale) of every sounding voice to out; n <= 128 */
void tx_render(tx_part_t *P, int32_t *out, uint32_t n);
/* the read position EVOL has reached, 0..65535 of the sample (for tests and the screen) */
uint32_t tx_position(const tx_part_t *P);
#endif
