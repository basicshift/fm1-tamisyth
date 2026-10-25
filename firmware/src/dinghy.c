/* SPDX-License-Identifier: GPL-3.0-only */
/* TAMISYNTH: pets that live in the M-VAVE FM-1, eat the patterns you make and teach you to make them.
 * Built on Dinghy (zvenson), on Felucca's platform (Leo Kuroshita / Hugelton Instruments) as SLOOP carries it.
 *
 * Pick a library (drums, percussion, bass, lead), get one of its three eggs at random, and raise it by learning
 * that library: every bar it hears, it eats; bars that meet its lesson grow it up. Later, a band of four pets,
 * one per library, plays together.
 *
 *   HOME           the nest: your pets, a new egg, the band
 *   nest / menus   OCT- OCT+ (or KNOB1) move, SEL opens
 *   raising        keys F3..G#4 the 16 steps; PLAY start / stop; KNOB2 tempo; KNOB3 swing; SAVE
 *     drums        OCT- OCT+ or KNOB1: the lane (kick, snare, hat); keys A4 A#4 B4 play them live
 *     bass         keys A4..G5 pick a note (held step keys take it); OCT- OCT+ the octave; KNOB1 nudges the note
 *   band           OCT- OCT+ pick a part, KNOB1 its pet, KNOB2 tempo, PLAY
 *
 * It lives on powered-on time (the FM-1 has no clock that runs while off): switched off, it sleeps.
 * Integers only, no 64-bit division (docs/RULES.md). */
#define APP_NAME "TAMISYNTH"
#define STORE_MAGIC 0x494D4154u                 /* "TAMI" */
#define CV_MAX (240u * 120u)
static void app_idle(void);
#define LCD_IDLE app_idle
#include "platform.c"
typedef struct { uint8_t w, h; const uint8_t *bits; } art_t;
#include "art.h"                               /* tools/mono.py, from the PNG strips in assets/art */

static void app_idle(void) {}

/* ======================================================================================= the sound (audio ISR) */
enum { LK, LS, LH, NLANE };
#define BEATS 0x1111u                           /* steps 1 5 9 13: the four beats */
#define BACK  0x1010u                           /* steps 5 13: beats 2 and 4 */
#define OFF8  0x4444u                           /* steps 3 7 11 15: the "and"s */

static volatile uint16_t pl_drum[NLANE];        /* what plays: drum lanes (bit i = step i) ... */
static volatile uint16_t pl_bgate;              /* ... the bass steps on ... */
static volatile uint8_t pl_bnote[16];           /* ... and their notes (MIDI) */
static volatile uint32_t pl_drums, pl_bass, pl_click;   /* which parts sound; a click on the beats */
static volatile uint32_t bpm = 100, swing = 0;  /* swing 0..128 = 0..50 % of a step */
static volatile uint32_t playing, step;
static volatile uint32_t trig_req;              /* bits 0..2 drums now, 3 the chirp, 4 the bass (b_req_note) */
static volatile uint32_t b_req_note;
static volatile uint32_t bar_seq;               /* + 1 at the end of every bar, with what that bar played: */
static volatile uint16_t bar_drum[NLANE], bar_bgate;
static volatile uint8_t bar_bnote[16];
static uint32_t samp_left, played;

static int32_t sine(uint32_t ph)                /* a full cycle over 2^32 -> -32767..32767 */
{
    int32_t x = (int32_t)ph >> 16, ax = x < 0 ? -x : x, y, ay;
    y = (x * (32768 - ax)) >> 13;
    ay = y < 0 ? -y : y;
    y += ((((y * ay) >> 15) - y) * 7373) >> 15;
    return y > 32767 ? 32767 : y < -32767 ? -32767 : y;
}
static uint32_t nz = 0x9E3779B9u;
static inline int32_t noise(void) { nz ^= nz << 13; nz ^= nz >> 17; nz ^= nz << 5; return (int32_t)nz >> 17; }

#define HZ(f) ((uint32_t)((f) * 97391u))        /* f * 2^32 / 44100, near enough */
static const uint32_t INC_C9[12] = {            /* MIDI 120..131 (C9..B9); lower octaves shift right */
    815363807u, 863847862u, 915214929u, 969636441u, 1027294024u, 1088380105u,
    1153098554u, 1221665363u, 1294309365u, 1371273005u, 1452813141u, 1539201906u};
static uint32_t note_inc(uint32_t m) { return m > 131u ? INC_C9[11] : INC_C9[m % 12u] >> (10u - m / 12u); }

static uint32_t k_ph, k_inc, k_amp, s_ph, s_tamp, s_namp, h_amp, c_ph, c_inc, c_amp;
static uint32_t b_ph, b_sph, b_inc, b_amp, b_cut, b_held;
static int32_t h_prev, b_y;

static void trig(uint32_t v)
{
    if (v == LK) { k_ph = 0; k_inc = HZ(165); k_amp = 26000; }
    else if (v == LS) { s_ph = 0; s_tamp = 20000; s_namp = 22000; }
    else if (v == LH) h_amp = 15000;
    else { c_ph = 0; c_inc = HZ(900); c_amp = 9000; }
}
static void trig_bass(uint32_t note) { b_inc = note_inc(note); b_amp = 24000; b_cut = 2600; b_held = 1; }

static void app_audio(int32_t *out, uint32_t n)
{
    uint32_t i, r = trig_req;
    if (r) {
        trig_req = 0;
        for (i = 0; i < 4u; i++) if (r >> i & 1u) trig(i);
        if (r & 16u) { trig_bass(b_req_note); b_held = 0; }
    }
    for (i = 0; i < n; i++) {
        int32_t s = 0, nn;
        if (playing) {
            if (samp_left == 0) {               /* the next step */
                uint32_t base = 661500u / bpm, st, l;   /* samples a 16th: 44100 * 15 / bpm */
                st = (step + 1u) & 15u;
                if (st == 0 && played) {
                    for (l = 0; l < NLANE; l++) bar_drum[l] = pl_drum[l];
                    bar_bgate = pl_bgate;
                    for (l = 0; l < 16u; l++) bar_bnote[l] = pl_bnote[l];
                    bar_seq++;
                }
                step = st;
                played = 1;
                if (pl_drums) for (l = 0; l < NLANE; l++) if (pl_drum[l] >> st & 1u) trig(l);
                if (pl_click && (st & 3u) == 0u) h_amp = st ? 5000 : 9000;
                if (pl_bass) { if (pl_bgate >> st & 1u) trig_bass(pl_bnote[st]); else b_held = 0; }
                samp_left = st & 1u ? base - (base * swing >> 8) : base + (base * swing >> 8);
            }
            samp_left--;
        }
        if (k_amp > 40u) {                      /* kick: a sine falling 165 -> 48 Hz */
            k_inc -= (k_inc - HZ(48)) >> 9;
            k_ph += k_inc;
            s += sine(k_ph) * (int32_t)k_amp >> 15;
            k_amp -= k_amp >> 12;
        }
        nn = noise();
        if (s_namp > 40u) {                     /* snare: a 185 Hz body and noise */
            s_ph += HZ(185);
            s += (sine(s_ph) * (int32_t)s_tamp >> 16) + (nn * (int32_t)s_namp >> 15);
            s_tamp -= s_tamp >> 9;
            s_namp -= s_namp >> 11;
        }
        if (h_amp > 40u) {                      /* hat: noise, differentiated (brighter) */
            s += (nn - h_prev) * (int32_t)h_amp >> 16;
            h_amp -= h_amp >> 9;
        }
        h_prev = nn;
        if (b_amp > 40u) {                      /* bass: a saw and a sub, through a closing low-pass */
            int32_t x;
            b_ph += b_inc;
            b_sph += b_inc >> 1;
            x = ((int32_t)b_ph >> 17) + (sine(b_sph) >> 2);
            b_cut -= (b_cut - 260u) >> 10;
            b_y += (x - b_y) * (int32_t)b_cut >> 12;
            s += b_y * (int32_t)b_amp >> 15;
            b_amp -= b_held ? b_amp >> 15 : b_amp >> 11;
        }
        if (c_amp > 40u) {                      /* the pet's chirp: a square rising */
            c_ph += c_inc;
            c_inc += c_inc >> 11;
            s += (int32_t)c_ph < 0 ? (int32_t)c_amp : -(int32_t)c_amp;
            c_amp -= c_amp >> 10;
        }
        s = s * 5 >> 3;                         /* headroom */
        out[2u * i] += s;
        out[2u * i + 1u] += s;
    }
}

/* ======================================================================================= the pets */
enum { LIB_DRUMS, LIB_PERC, LIB_BASS, LIB_LEAD, NLIB };
enum { ST_EGG, ST_BABY, ST_CHILD, ST_TEEN, ST_ADULT };
#define NPET 8u
static const char *const LIB_NAME[NLIB] = {"DRUMS", "PERCUSSION", "BASS", "LEAD"};
static const uint8_t LIB_READY[NLIB] = {1, 0, 1, 0};
static const char *const EGG_NAME[NLIB][3] = {   /* (the second and third of each: placeholder names) */
    {"TOMCAT", "KITBASH", "RIMSHOT"}, {"?", "?", "?"},
    {"DEEP DRAGON", "EMBER DRAGON", "FROST DRAGON"}, {"?", "?", "?"}};
static const char *const STAGE_WORD[4] = {"egg", "baby", "child", "teen"};
static const char *const FORM_NAME[NLIB][4] = {
    {"boom bap", "house", "breaks", "dub"}, {"", "", "", ""}, {"sub", "acid", "disco", "funk"}, {"", "", "", ""}};
static const char *const LESSON[NLIB][4] = {
    {"Hatch me: kick every beat", "Song: Stomp stomp clap", "Song: Rock beat", "Kick off the beat, or swing"},
    {"", "", "", ""},
    {"Hatch me: a note on step 1", "Song: Twinkle Twinkle", "Song: Mountain King", "Notes on the off-beats"},
    {"", "", "", ""}};
static const char *const ADULT_TIP[NLIB][4] = {
    {"Boom bap: swing it, lay back", "House: 4 kicks, offbeat hats", "Breaks: busy snares, odd kicks", "Dub: less is more"},
    {"", "", "", ""},
    {"Sub: few notes, let them ring", "Acid: ten notes or more", "Disco: bounce in octaves", "Funk: walk it, syncopate"},
    {"", "", "", ""}};
static const uint32_t NEED[4] = {4, 12, 16, 24};   /* good bars to grow out of each stage */

typedef struct {
    uint8_t used, lib, type, stage, form, bpm, swing, oct;
    uint16_t hunger, mood;                      /* 0..100 */
    uint32_t age_s, prog, bars;
    uint16_t diet[4];
    uint16_t lane[NLANE];                       /* drums: the three lanes */
    uint16_t gate;                              /* bass: the steps on ... */
    uint8_t note[16];                           /* ... and their notes */
} pet_t;
static struct save {
    uint32_t version;
    uint8_t cur, band_bpm, pad[2];
    uint8_t band[NLIB];                         /* the band: pet index + 1 per part, 0 = none */
    pet_t pet[NPET];
} sv;
#define SAVE_VERSION 2u
static pet_t *P;                                /* the pet being raised */

/* ---- training songs: the baby and child stages learn a tune you may know (public domain or a bare rhythm).
 * Steps are 16ths; bass notes are semitones above C, compared in any octave. */
typedef struct { const char *name; uint16_t lane[NLANE]; uint16_t gate; uint8_t note[16]; } song_t;
static const song_t SONG_STOMP = {"Stomp stomp clap", {0x0505u, 0x1010u, 0}, 0, {0}};          /* boom boom CLAP */
static const song_t SONG_ROCK = {"Rock beat", {0x0501u, 0x1010u, 0x5555u}, 0, {0}};
static const song_t SONG_TWINKLE = {"Twinkle Twinkle", {0, 0, 0}, 0x1555u,                   /* C C G G A A G */
                                    {0, 0, 0, 0, 7, 0, 7, 0, 9, 0, 9, 0, 7, 0, 0, 0}};
static const song_t SONG_MOUNTAIN = {"Mountain King", {0, 0, 0}, 0x1555u,                    /* Grieg, 1875 */
                                     {0, 0, 2, 0, 3, 0, 5, 0, 7, 0, 3, 0, 7, 0, 0, 0}};
static const song_t *song_for(uint32_t lib, uint32_t stage)
{
    if (stage == 1u) return lib == 0u ? &SONG_STOMP : &SONG_TWINKLE;
    if (stage == 2u) return lib == 0u ? &SONG_ROCK : &SONG_MOUNTAIN;
    return 0;
}

static uint32_t pc16(uint32_t v) { uint32_t c = 0; v &= 0xFFFFu; while (v) { v &= v - 1u; c++; } return c; }

static uint32_t drum_form(const uint16_t *b)
{
    uint32_t hits = pc16(b[LK]) + pc16(b[LS]) + pc16(b[LH]);
    if (hits <= 6u) return 3;                                           /* dub */
    if ((b[LK] & BEATS) == BEATS && pc16(b[LH] & OFF8) >= 2u) return 1; /* house */
    if (pc16(b[LS]) >= 4u || pc16(b[LK] & ~BEATS) >= 2u) return 2;      /* breaks */
    return 0;                                                           /* boom bap */
}
static int drum_ok(const pet_t *p, const uint16_t *b)
{
    switch (p->stage) {
    case ST_EGG:   return (b[LK] & BEATS) == BEATS;
    case ST_BABY:
    case ST_CHILD: {                            /* the song's hits all there, at most two extra */
        const song_t *sg = song_for(LIB_DRUMS, p->stage);
        uint32_t l, extra = 0;
        for (l = 0; l < NLANE; l++) {
            if ((b[l] & sg->lane[l]) != sg->lane[l]) return 0;
            extra += pc16(b[l] & ~sg->lane[l]);
        }
        return extra <= 2u;
    }
    case ST_TEEN:  return (b[LS] & BACK) == BACK && ((b[LK] & ~BEATS & 0xFFFFu) || p->swing >= 32u);
    default:       return drum_form(b) == p->form;
    }
}

typedef struct { uint32_t n, distinct, related, octj; } bassinfo_t;
static bassinfo_t bass_look(uint32_t gate, const uint8_t *note)
{
    bassinfo_t r = {0, 0, 0, 0};
    uint32_t i, j, prev = 0, have = 0;
    for (i = 0; i < 16u; i++) {
        if (!(gate >> i & 1u)) continue;
        r.n++;
        for (j = 0; j < i; j++) if ((gate >> j & 1u) && note[j] == note[i]) break;
        if (j == i) r.distinct++;
        for (j = 0; j < i; j++)
            if (gate >> j & 1u) {
                uint32_t d = note[i] > note[j] ? note[i] - note[j] : note[j] - note[i];
                if (d == 7u || d == 12u || d == 5u) r.related = 1;
            }
        if (have && (note[i] == prev + 12u || prev == note[i] + 12u)) r.octj++;
        prev = note[i]; have = 1;
    }
    return r;
}
static uint32_t bass_form(const bassinfo_t *b)
{
    if (b->n <= 4u) return 0;                   /* sub */
    if (b->n >= 10u) return 1;                  /* acid */
    if (b->octj >= 3u) return 2;                /* disco */
    return 3;                                   /* funk */
}
static int song_bass_ok(const song_t *sg, uint32_t gate, const uint8_t *note)
{
    uint32_t i, extra = 0;
    for (i = 0; i < 16u; i++) {
        if (sg->gate >> i & 1u) { if (!(gate >> i & 1u) || note[i] % 12u != sg->note[i]) return 0; }
        else if (gate >> i & 1u) extra++;
    }
    return extra <= 1u;
}
static int bass_ok(const pet_t *p, uint32_t gate, const uint8_t *note, const bassinfo_t *b)
{
    switch (p->stage) {
    case ST_EGG:   return gate & 1u;
    case ST_BABY:
    case ST_CHILD: return song_bass_ok(song_for(LIB_BASS, p->stage), gate, note);
    case ST_TEEN:  return (gate & OFF8) != 0u && b->distinct >= 2u;
    default:       return bass_form(b) == p->form;
    }
}

static char say[32];
static uint32_t say_until, redraw, dirty, happy_until, feed_until, cheer_until;
static void put(char *d, const char *s) { while ((*d++ = *s++) != 0) {} }
static void say_it(const char *s, uint32_t ms) { put(say, s); say_until = fm1_ms + ms; redraw |= 2u; }

static uint32_t rng_x;
#ifdef DINGHY_HOST
#define fm1_ticks() (fm1_ms * 24000u)
#endif
static uint32_t rnd(void) { rng_x ^= fm1_ticks(); rng_x ^= rng_x << 13; rng_x ^= rng_x >> 17; rng_x ^= rng_x << 5; return rng_x; }

static int new_pet(uint32_t lib)
{
    uint32_t i;
    for (i = 0; i < NPET; i++) if (!sv.pet[i].used) break;
    if (i == NPET) return -1;
    memset(&sv.pet[i], 0, sizeof sv.pet[i]);
    sv.pet[i].used = 1;
    sv.pet[i].lib = (uint8_t)lib;
    sv.pet[i].type = (uint8_t)(rnd() % 3u);
#ifdef DINGHY_HOST
    if (getenv("TAMI_TYPE")) sv.pet[i].type = (uint8_t)(atoi(getenv("TAMI_TYPE")) % 3);   /* tests: pick the egg */
#endif
    sv.pet[i].mood = 50;
    sv.pet[i].bpm = 100;
    if (lib == LIB_DRUMS) sv.pet[i].lane[LK] = 0x0001u;
    for (uint32_t s = 0; s < 16u; s++) sv.pet[i].note[s] = 36;   /* C2 */
    return (int)i;
}

static void eat_bar(void)
{
    uint16_t d[NLANE];
    uint8_t bn[16];
    uint32_t i, hits, ok, f = 0, gate = bar_bgate;
    bassinfo_t bi = {0, 0, 0, 0};
    for (i = 0; i < NLANE; i++) d[i] = bar_drum[i];
    for (i = 0; i < 16u; i++) bn[i] = bar_bnote[i];
    if (P->lib == LIB_DRUMS) { hits = pc16(d[LK]) + pc16(d[LS]) + pc16(d[LH]); ok = drum_ok(P, d); f = drum_form(d); }
    else { bi = bass_look(gate, bn); hits = bi.n; ok = bass_ok(P, gate, bn, &bi); f = bass_form(&bi); }
    P->bars++;
    if (P->stage != ST_EGG && hits >= 3u) {
        P->hunger = P->hunger > 3u ? (uint16_t)(P->hunger - 3u) : 0u;
        feed_until = fm1_ms + 1200u;
        if (ok && P->hunger) P->hunger--;
    }
    if (P->stage == ST_TEEN && hits >= 3u && P->diet[f] < 60000u) P->diet[f]++;
    if (ok) {
        P->mood = P->mood + 4u > 100u ? 100u : (uint16_t)(P->mood + 4u);
        if (P->stage < ST_ADULT) P->prog++;
        trig_req |= 8u;
        happy_until = fm1_ms + 1500u;
        say_it(P->stage == ST_EGG ? "*tap tap* (it moved!)" : "Yum! That's the one", 1800);
    } else {
        if (P->mood) P->mood--;
        if (hits < 3u && P->stage != ST_EGG) say_it("...is that all?", 1800);
    }
    if (P->stage < ST_ADULT && P->prog >= NEED[P->stage]) {
        P->stage++;
        P->prog = 0;
        if (P->stage == ST_ADULT) {
            uint32_t best = 0;
            for (i = 1; i < 4u; i++) if (P->diet[i] > P->diet[best]) best = i;
            P->form = (uint8_t)best;
        }
        say_it(P->stage == ST_BABY ? "It hatched!" : "It grew!", 4000);
        cheer_until = fm1_ms + 4000u;
        redraw = 0xFFu;
        dirty |= 2u;
    }
    dirty |= 1u;
}

static void tick_second(void)
{
    P->age_s++;
    if (P->stage != ST_EGG && P->age_s % 15u == 0u) {
        if (P->hunger < 100u) P->hunger++;
        if (P->hunger > 70u && P->mood) P->mood--;
        if (P->hunger == 80u) say_it("I'm hungry. Play for me", 3000);
        dirty |= 1u;
        redraw |= 1u;
    }
}

/* ======================================================================================= the screen */
#define C_BG    RGB(10, 13, 9)                  /* the 1-bit look: ink on a dark LCD */
#define C_INK    RGB(214, 234, 160)
#define C_LO   RGB(70, 84, 52)
enum { M_RAISE, M_NEST, M_PICK, M_HATCH, M_BAND };
static uint32_t mode = M_NEST, cursor, lane, cur_note = 36, stop_ms, anim, t_sec;

static char *put_u(char *p, uint32_t v)
{
    char t[12];
    int n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (n) *p++ = t[--n];
    *p = 0;
    return p;
}
static char *cat(char *p, const char *s) { while (*s) *p++ = *s++; *p = 0; return p; }

static void blit_art(const art_t *a, int32_t ox, int32_t oy, int32_t sc, uint16_t c)
{
    uint32_t x, y, bpr = (a->w + 7u) / 8u;
    for (y = 0; y < a->h; y++)
        for (x = 0; x < a->w; x++)
            if (a->bits[y * bpr + x / 8u] & (0x80u >> (x & 7u)))
                cv_rect(ox + (int32_t)x * sc, oy + (int32_t)y * sc, sc, sc, c);
}
static int asleep(void) { return mode == M_RAISE && !playing && fm1_ms - stop_ms > 60000u; }
typedef struct { uint8_t idle0, idle1, blink, happy, dance, sleep, sad, cheer; } anim_t;   /* pairs / a triple follow */
static const anim_t DRAGON_ANIM = {A_DEEP_DRAGON_IDLE0, A_DEEP_DRAGON_IDLE1, A_DEEP_DRAGON_BLINK, A_DEEP_DRAGON_HAPPY0,
                                   A_DEEP_DRAGON_DANCE0, A_DEEP_DRAGON_SLEEP0, A_DEEP_DRAGON_SAD0, A_DEEP_DRAGON_CHEER0};
static const anim_t TOMCAT_ANIM = {A_TOMCAT_IDLE0, A_TOMCAT_IDLE1, A_TOMCAT_BLINK, A_TOMCAT_HAPPY0,
                                   A_TOMCAT_DANCE0, A_TOMCAT_SLEEP0, A_TOMCAT_SAD0, A_TOMCAT_CHEER0};
static const anim_t *anim_of(const pet_t *p)    /* the creatures drawn so far; the others are placeholders */
{
    if (p->lib == LIB_DRUMS && p->type == 0u) return &TOMCAT_ANIM;
    if (p->lib == LIB_BASS) return &DRAGON_ANIM;
    return 0;
}
static const art_t *pet_art(const pet_t *p, int dancing)
{
    static const uint8_t KID[4] = {A_DRUM_LINE_EGG, A_DRUM_LINE_BLIP, A_DRUM_LINE_KIK, A_DRUM_LINE_HATLING};
    static const uint8_t ADULT[4] = {A_DRUM_LINE_CRATE, A_DRUM_LINE_FOURFLOOR, A_DRUM_LINE_CHOPPER, A_DRUM_LINE_ECHO};
    const anim_t *an = anim_of(p);
    int mine = p == P && mode == M_RAISE;
    if (p->stage == ST_EGG) return &ART[A_DRUM_LINE_EGG];
    if (!an) return &ART[p->stage < ST_ADULT ? KID[p->stage] : ADULT[p->form & 3u]];
    if (mine && fm1_ms < cheer_until) return &ART[an->cheer + (anim & 1u)];
    if (mine && fm1_ms < happy_until) return &ART[an->happy + (anim & 1u)];
    if (dancing) return &ART[an->dance + anim % 3u];
    if (mine && asleep()) return &ART[an->sleep + (anim >> 2 & 1u)];
    if (p->hunger > 70u || p->mood < 30u) return &ART[an->sad + (anim >> 1 & 1u)];
    {
        uint8_t seq[8] = {an->idle0, an->idle1, an->idle0, an->blink, an->idle1, an->idle0, an->idle1, an->idle0};
        return &ART[seq[anim & 7u]];
    }
}
static void name_of(char *b, const pet_t *p)
{
    char *q = cat(b, EGG_NAME[p->lib][p->type % 3u]);
    q = cat(q, "  ");
    cat(q, p->stage < ST_ADULT ? STAGE_WORD[p->stage] : FORM_NAME[p->lib][p->form & 3u]);
}
static const uint8_t HEART[6] = {0x6C, 0xFE, 0xFE, 0x7C, 0x38, 0x10};   /* 7 x 6 */
static void heart(int32_t x, int32_t y, uint16_t c)
{
    uint32_t r, k;
    for (r = 0; r < 6u; r++) for (k = 0; k < 7u; k++) if (HEART[r] & (0x80u >> k)) cv_pset(x + (int32_t)k, y + (int32_t)r, c);
}
static void box(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    cv_rect(x, y, w, 1, c); cv_rect(x, y + h - 1, w, 1, c); cv_rect(x, y, 1, h, c); cv_rect(x + w - 1, y, 1, h, c);
}

static void draw_icons(void)                    /* 0..28: what the pet is doing, Tamagotchi style */
{
    static const uint8_t ICON[6] = {A_ICONS_FEED, A_ICONS_PLAY, A_ICONS_LEARN, A_ICONS_SLEEP, A_ICONS_LOVE, A_ICONS_HOME};
    int on[6];
    uint32_t k;
    on[0] = fm1_ms < feed_until; on[1] = (int)playing; on[2] = fm1_ms < happy_until || fm1_ms < cheer_until;
    on[3] = asleep(); on[4] = P->stage != ST_EGG && P->mood >= 70u; on[5] = 0;
    cv_begin(240, 28, C_BG);
    for (k = 0; k < 6u; k++) {
        const art_t *a = &ART[ICON[k]];
        int32_t cx = 20 + (int32_t)k * 40;
        blit_art(a, cx - (int32_t)a->w / 2, 2, 1, on[k] ? C_INK : C_LO);
        if (on[k]) cv_rect(cx - 10, 25, 20, 2, C_INK);
    }
    cv_rect(0, 27, 240, 1, C_LO);
    cv_blit(0, 0);
}
static void draw_top(void)                      /* 28..46: name, then food pips and joy hearts */
{
    uint32_t k;
    cv_begin(240, 18, C_BG);
    cv_text(6, 1, &FONT_S, EGG_NAME[P->lib][P->type % 3u], C_INK);
    if (P->stage != ST_EGG) {
        uint32_t fed = (100u - P->hunger + 10u) / 20u, joy = (P->mood + 10u) / 20u;
        for (k = 0; k < 5u; k++) {
            if (k < fed) cv_rect(150 + (int32_t)k * 8, 6, 6, 6, C_INK); else box(150 + (int32_t)k * 8, 6, 6, 6, C_LO);
            heart(194 + (int32_t)k * 9, 6, k < joy ? C_INK : C_LO);
        }
    }
    cv_blit(0, 28);
    draw_icons();
}
static void draw_pet(int bob)                   /* 46..158 */
{
    const art_t *a = pet_art(P, playing);
    char b[24], *p;
    int32_t sc = a->h * 3u <= 100u ? 3 : 2, w = (int32_t)a->w * sc, h = (int32_t)a->h * sc;
    cv_begin(240, 112, C_BG);
    cv_text(4, 0, &FONT_S, P->stage < ST_ADULT ? STAGE_WORD[P->stage] : FORM_NAME[P->lib][P->form & 3u], C_LO);
    p = cat(put_u(b, bpm), " bpm");
    if (swing) { p = cat(p, " sw"); cat(put_u(p, swing * 100u / 256u), "%"); }
    cv_text(236 - text_w(&FONT_S, b), 0, &FONT_S, b, C_LO);
    cv_rect(40, 108, 160, 1, C_LO);
    blit_art(a, (240 - w) / 2, 107 - h - (bob && !anim_of(P) ? 3 : 0), sc, C_INK);
    if (!anim_of(P) && asleep()) cv_text(176, 30, &FONT_S, "z z", C_LO);
    cv_blit(0, 46);
}
/* ---- the guide: what to do next, in words, with the keys and the button to press blinking */
#define NO_BTN 0xFFu
static const char *guide_txt;                  /* 0: nothing to point at, show the lesson */
static uint16_t guide_bits;                     /* steps to tap in the selected lane (they blink) */
static uint32_t guide_btn = NO_BTN;             /* a button to press (it blinks) */
static uint32_t guide_note = 0xFFu;            /* bass: the note key to press, semitones above the key root */
static const char *const PICK_ROW[NLANE] = {"Pick the KICK row: OCT-", "Pick the SNARE row: OCT", "Pick the HAT row: OCT+"};
static int lesson_now(void)                     /* would the pattern as it stands pass the lesson? */
{
    if (P->lib == LIB_DRUMS) return drum_ok(P, P->lane);
    { bassinfo_t bi = bass_look(P->gate, P->note); return bass_ok(P, P->gate, P->note, &bi); }
}
static void compute_guide(void)
{
    uint16_t want[NLANE] = {0, 0, 0};
    uint32_t l;
    guide_txt = 0; guide_bits = 0; guide_btn = NO_BTN; guide_note = 0xFFu;
    if (P->stage < ST_ADULT) {
        if (P->lib == LIB_DRUMS) {
            switch (P->stage) {
            case ST_EGG:   want[LK] = BEATS; break;
            case ST_BABY:
            case ST_CHILD: { const song_t *sg = song_for(LIB_DRUMS, P->stage); for (l = 0; l < NLANE; l++) want[l] = sg->lane[l]; break; }
            default:       want[LS] = BACK; if (!(P->lane[LK] & ~BEATS & 0xFFFFu) && P->swing < 32u) want[LK] = 1u << 10; break;
            }
            for (l = 0; l < NLANE; l++) {
                uint16_t miss = (uint16_t)(want[l] & ~P->lane[l]);
                if (!miss) continue;
                if (l != lane) {
                    guide_txt = l == LS ? (lane < LS ? "Pick the SNARE row: OCT+" : "Pick the SNARE row: OCT-") : PICK_ROW[l];
                    guide_btn = l > lane ? BTN_OCT_UP : BTN_OCT_DOWN;
                } else { guide_bits = miss; guide_txt = "Tap the blinking keys"; }
                return;
            }
        } else {
            uint16_t w = 0;
            switch (P->stage) {
            case ST_EGG:   w = 1u; break;
            case ST_BABY:
            case ST_CHILD: {                    /* the song, one note at a time: pick the note, then tap its step */
                const song_t *sg = song_for(LIB_BASS, P->stage);
                for (l = 0; l < 16u; l++) {
                    if (sg->gate >> l & 1u) {
                        if ((P->gate >> l & 1u) && P->note[l] % 12u == sg->note[l]) continue;
                        if (cur_note % 12u != sg->note[l]) { guide_note = sg->note[l]; guide_txt = "Press the blinking note"; }
                        else { guide_bits = (uint16_t)(1u << l); guide_txt = "Tap the blinking step"; }
                        return;
                    }
                    if (P->gate >> l & 1u) { guide_bits = (uint16_t)(1u << l); guide_txt = "Tap to take this one off"; return; }
                }
                break;
            }
            default:       if (!(P->gate & OFF8)) w = 1u << 14; break;
            }
            if (w & ~P->gate) { guide_bits = (uint16_t)(w & ~P->gate); guide_txt = "Tap the blinking keys"; return; }
        }
        if (!lesson_now()) return;              /* the lesson line says what is still missing */
    }
    if (!playing) { guide_txt = P->stage == ST_EGG ? "Now press PLAY" : "Press PLAY to feed it"; guide_btn = BTN_PLAY; }
}

static void draw_words(void)                    /* 158..192 */
{
    cv_begin(240, 34, C_BG);
    if (fm1_ms < say_until) cv_text(6, 2, &FONT_S, say, C_INK);
    else if (guide_txt) {                       /* the next thing to do, with an arrow */
        cv_rect(6, 6, 8, 8, C_INK);
        cv_text(20, 2, &FONT_S, guide_txt, C_INK);
    } else cv_text(6, 2, &FONT_S, P->stage < ST_ADULT ? LESSON[P->lib][P->stage] : ADULT_TIP[P->lib][P->form & 3u], C_INK);
    if (P->stage < ST_ADULT) {
        box(6, 24, 228, 6, C_LO);
        cv_rect(7, 25, (int32_t)(226u * P->prog / NEED[P->stage]), 4, C_INK);
    }
    cv_blit(0, 158);
}
static void draw_grid(void)                     /* 192..240 */
{
    uint32_t l, s;
    cv_begin(240, 48, C_BG);
    if (P->lib == LIB_DRUMS) {
        for (l = 0; l < NLANE; l++) {
            int32_t y = 2 + (int32_t)l * 15;
            const char *lab = l == 0 ? "K" : l == 1 ? "S" : "H";
            if (l == lane) cv_rect(4, y, 24, 13, C_INK); else box(4, y, 24, 13, C_LO);
            cv_text(12, y - 2, &FONT_S, lab, l == lane ? C_BG : C_LO);
            for (s = 0; s < 16u; s++) {
                int32_t x = 32 + (int32_t)s * 13 + (int32_t)(s / 4u);
                int on = P->lane[l] >> s & 1u, here = playing && s == step;
                if (on) cv_rect(x, y, 11, 13, here ? C_LO : C_INK);
                else {
                    box(x, y, 11, 13, s % 4u == 0 ? C_INK : C_LO);
                    if (here) cv_rect(x + 2, y + 2, 7, 9, C_LO);
                    if (l == lane && (guide_bits >> s & 1u)) cv_rect(x + 4, y + 5, 3, 3, C_INK);   /* tap here */
                }
            }
        }
    } else {                                    /* bass: a column per step, the note as height */
        char b[8];
        static const char *const NN[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        put_u(cat(b, NN[cur_note % 12u]), cur_note / 12u - 1u);
        cv_rect(4, 2, 24, 44, C_INK);
        cv_text(16 - text_w(&FONT_S, b) / 2, 6, &FONT_S, b, C_BG);
        if (guide_note != 0xFFu) { cv_text(16 - text_w(&FONT_S, NN[guide_note % 12u]) / 2, 26, &FONT_S, NN[guide_note % 12u], C_BG); cv_rect(6, 24, 20, 1, C_BG); }
        for (s = 0; s < 16u; s++) {
            int32_t x = 32 + (int32_t)s * 13 + (int32_t)(s / 4u);
            int here = playing && s == step;
            cv_rect(x, 45, 11, 1, s % 4u == 0 ? C_INK : C_LO);
            if (P->gate >> s & 1u) {
                int32_t hgt = 6 + ((int32_t)P->note[s] - 24);
                if (hgt > 42) hgt = 42;
                if (hgt < 4) hgt = 4;
                if (here) box(x, 44 - hgt, 11, hgt, C_INK); else cv_rect(x, 44 - hgt, 11, hgt, C_INK);
            } else if (here) cv_rect(x, 40, 11, 4, C_LO);
            if (guide_bits >> s & 1u) { box(x, 30, 11, 11, C_INK); cv_rect(x + 4, 34, 3, 3, C_INK); }   /* tap here */
        }
    }
    cv_blit(0, 192);
}

static void draw_list(const char *title, const char *const *rows, const uint8_t *dim, const uint8_t *icon, uint32_t n,
                      int32_t rowh, const char *hint)
{
    uint32_t i;
    lcd_fill(0, 0, 240, 240, C_BG);
    draw_text_box(0, 6, 240, &FONT_L, title, C_INK, 1);
    lcd_fill(0, 40, 240, 1, C_LO);
    for (i = 0; i < n; i++) {
        int32_t y = 46 + (int32_t)i * rowh, tx = 10;
        uint16_t bg = i == cursor ? C_INK : C_BG, fg = i == cursor ? C_BG : dim && dim[i] ? C_LO : C_INK;
        cv_begin(232, (uint32_t)rowh - 1u, bg);
        if (icon && icon[i] != 0xFF) { const art_t *a = &ART[icon[i]]; blit_art(a, 4, (rowh - 1 - (int32_t)a->h) / 2, 1, fg); tx = 36; }
        cv_text(tx, (rowh - 1 - 16) / 2, &FONT_S, rows[i], fg);
        cv_blit(4, (uint32_t)y);
    }
    draw_text_box(0, 222, 240, &FONT_S, hint, C_LO, 1);
}

static char rowbuf[10][32];
static const char *rowp[10];
static uint8_t rowdim[10], rowpet[10], rowicon[10];
static uint32_t nrows, release_ms;
static const uint8_t LIB_ICON[NLIB] = {A_ICONS_DRUMS, A_ICONS_STAR, A_ICONS_GUITAR, A_ICONS_PIANO};

static void build_nest(void)
{
    uint32_t i, hasfree = 0;
    nrows = 0;
    for (i = 0; i < NPET; i++) {
        if (!sv.pet[i].used) { hasfree = 1; continue; }
        name_of(rowbuf[nrows], &sv.pet[i]);
        rowdim[nrows] = 0; rowpet[nrows] = (uint8_t)i; nrows++;
    }
    if (hasfree) { put(rowbuf[nrows], "+ new egg"); rowdim[nrows] = 0; rowpet[nrows] = 0xFE; nrows++; }
    put(rowbuf[nrows], "band"); rowdim[nrows] = 0; rowpet[nrows] = 0xFD; nrows++;
    for (i = 0; i < nrows; i++) rowp[i] = rowbuf[i];
    if (cursor >= nrows) cursor = nrows - 1u;
}
static void draw_nest(void)
{
    build_nest();
    draw_list("NEST", rowp, rowdim, 0, nrows, 17,
              release_ms && fm1_ms - release_ms < 3000u ? "SEQ again: release it"
              : nrows <= 2u ? "Press SEL to get an egg" : "SEL open   SEQ release");
}
static void draw_pick(void)
{
    uint32_t i;
    for (i = 0; i < NLIB; i++) {
        char *q = cat(rowbuf[i], LIB_NAME[i]);
        if (!LIB_READY[i]) cat(q, "  (soon)");
        rowp[i] = rowbuf[i]; rowdim[i] = (uint8_t)!LIB_READY[i]; rowicon[i] = LIB_ICON[i];
    }
    draw_list("PICK A LIBRARY", rowp, rowdim, rowicon, NLIB, 30, "OCT move   SEL pick");
}
static void draw_hatch(void)
{
    char b[40];
    const art_t *a = &ART[A_DRUM_LINE_EGG];
    lcd_fill(0, 0, 240, 240, C_BG);
    draw_text_box(0, 8, 240, &FONT_S, "You got", C_LO, 1);
    draw_text_box(0, 26, 240, &FONT_L, EGG_NAME[P->lib][P->type % 3u], C_INK, 1);
    cv_begin(240, 120, C_BG);
    blit_art(a, (240 - (int32_t)a->w * 3) / 2, 118 - (int32_t)a->h * 3, 3, C_INK);
    cv_blit(0, 66);
    cat(cat(b, LIB_NAME[P->lib]), " egg");
    draw_text_box(0, 194, 240, &FONT_S, b, C_INK, 1);
    draw_text_box(0, 218, 240, &FONT_S, "Press SEL to start", C_LO, 1);
}
static void draw_band(int bob)
{
    uint32_t l;
    char b[40];
    cv_begin(240, 28, C_BG);
    cv_text(6, 4, &FONT_S, "BAND", C_INK);
    cat(put_u(b, bpm), " bpm");
    cv_text(234 - text_w(&FONT_S, b), 4, &FONT_S, b, C_LO);
    cv_rect(0, 27, 240, 1, C_LO);
    cv_blit(0, 0);
    cv_begin(240, 120, C_BG);
    for (l = 0; l < NLIB; l++) {
        int32_t cx = 30 + (int32_t)l * 60;
        const art_t *ic = &ART[LIB_ICON[l]];
        if (l == cursor) box(cx - 29, 4, 58, 116, C_INK);
        if (sv.band[l]) {
            const pet_t *p = &sv.pet[sv.band[l] - 1u];
            const art_t *a = pet_art(p, playing);
            int32_t sc = a->w * 2u <= 54u && a->h * 2u <= 84u ? 2 : 1;
            blit_art(a, cx - (int32_t)a->w * sc / 2, 90 - (int32_t)a->h * sc - (bob && playing ? 2 : 0), sc, C_INK);
        }
        blit_art(ic, cx - (int32_t)ic->w / 2, 93, 1, LIB_READY[l] ? C_INK : C_LO);
    }
    cv_blit(0, 28);
    cv_begin(240, 80, C_BG);
    if (sv.band[cursor]) name_of(b, &sv.pet[sv.band[cursor] - 1u]);
    else put(b, LIB_READY[cursor] ? "(empty: KNOB1 picks)" : "(this library: soon)");
    cv_text(6, 10, &FONT_S, b, C_INK);
    cv_text(6, 54, &FONT_S, "OCT part  KNOB1 pet  PLAY", C_LO);
    cv_blit(0, 160);
}

/* ======================================================================================= main */
static void save_now(void) { settings_save(&sv, sizeof sv); dirty = 0; }

static void load_play_raise(void)               /* the pet being raised is what plays */
{
    uint32_t i;
    pl_drums = P->lib == LIB_DRUMS; pl_bass = P->lib == LIB_BASS; pl_click = P->lib != LIB_DRUMS;
    for (i = 0; i < NLANE; i++) pl_drum[i] = P->lane[i];
    pl_bgate = P->gate;
    for (i = 0; i < 16u; i++) pl_bnote[i] = P->note[i];
    bpm = P->bpm >= 60u && P->bpm <= 180u ? P->bpm : 100u;
    swing = P->swing <= 128u ? P->swing : 0u;
}
static void load_play_band(void)
{
    uint32_t i;
    pl_drums = 0; pl_bass = 0; pl_click = 0;
    if (sv.band[LIB_DRUMS]) { const pet_t *p = &sv.pet[sv.band[LIB_DRUMS] - 1u]; for (i = 0; i < NLANE; i++) pl_drum[i] = p->lane[i]; pl_drums = 1; }
    if (sv.band[LIB_BASS]) { const pet_t *p = &sv.pet[sv.band[LIB_BASS] - 1u]; pl_bgate = p->gate; for (i = 0; i < 16u; i++) pl_bnote[i] = p->note[i]; pl_bass = 1; }
    bpm = sv.band_bpm >= 60u && sv.band_bpm <= 180u ? sv.band_bpm : 100u;
    swing = 0;
}
static void go(uint32_t m)
{
    if (playing) { playing = 0; b_held = 0; }
    if (dirty) save_now();
    mode = m;
    cursor = 0;
    if (m == M_RAISE) load_play_raise();
    if (m == M_BAND) load_play_band();
    stop_ms = t_sec = fm1_ms;
    redraw = 0xFFu;
    for (uint32_t l = 0; l < 27u; l++) fm1_led_key(KEY_LED(l), 0);
    fm1_led_key(BTN_PLAY, 0); fm1_led_key(BTN_SEL, 0); fm1_led_key(BTN_OCT_UP, 0); fm1_led_key(BTN_OCT_DOWN, 0);
}

static void band_cycle(uint32_t part, int32_t dir)   /* the next pet of this library for this part, or none */
{
    int32_t i = sv.band[part] ? (int32_t)sv.band[part] - 1 : -1, k;
    for (k = 0; k <= (int32_t)NPET; k++) {
        i += dir;
        if (i < -1) i = (int32_t)NPET - 1;
        if (i >= (int32_t)NPET) i = -1;
        if (i == -1 || (sv.pet[i].used && sv.pet[i].lib == part && sv.pet[i].stage > ST_EGG)) break;
    }
    sv.band[part] = (uint8_t)(i + 1);
    dirty |= 1u;
    load_play_band();
}

static void app_main(void)
{
    uint32_t last_step = 99, last_seq, t_save = fm1_ms, t_anim = fm1_ms, prev_keys = 0, l;
    int bob = 0;

    rng_x = 0x2545F491u ^ fm1_ticks();
    if (settings_load(&sv, sizeof sv) || sv.version != SAVE_VERSION) { memset(&sv, 0, sizeof sv); sv.version = SAVE_VERSION; sv.band_bpm = 100; }
    if (sv.cur < NPET && sv.pet[sv.cur].used) { P = &sv.pet[sv.cur]; go(M_RAISE); say_it("*yawn* ...hi!", 2500); }
    else { P = &sv.pet[0]; go(M_NEST); }
    last_seq = bar_seq;

    for (;;) {
        uint32_t rel, b, keys, down;
        int32_t e, e2, mv = 0;
        platform_poll();
        b = fm1_input_edges(&rel);
        keys = fm1_in.notes;
        down = (keys & ~prev_keys) | fm1_input_note_edges();
        prev_keys = keys;
        if (b & (1u << BTN_OCT_DOWN)) mv = -1;
        if (b & (1u << BTN_OCT_UP)) mv = 1;
        e = fm1_enc_take(ENC_KNOB1);

        if (b & (1u << BTN_HOME) && mode != M_NEST) { go(M_NEST); continue; }

        if (mode == M_NEST || mode == M_PICK) {
            uint32_t n = mode == M_NEST ? (build_nest(), nrows) : NLIB;
            if (e) mv = e > 0 ? 1 : -1;
            if (mv) { cursor = (cursor + n + (uint32_t)mv) % n; release_ms = 0; redraw |= 1u; }
            if (mode == M_NEST && (b & (1u << BTN_SEQ)) && rowpet[cursor] < NPET) {   /* release a pet: SEQ twice */
                if (release_ms && fm1_ms - release_ms < 3000u) {
                    uint32_t t = rowpet[cursor], l2;
                    for (l2 = 0; l2 < NLIB; l2++) if (sv.band[l2] == t + 1u) sv.band[l2] = 0;
                    memset(&sv.pet[t], 0, sizeof sv.pet[t]);
                    if (sv.cur == t) sv.cur = 0xFF;
                    release_ms = 0; save_now();
                } else release_ms = fm1_ms;
                redraw |= 1u;
            }
            if (release_ms && fm1_ms - release_ms >= 3000u) { release_ms = 0; redraw |= 1u; }
            if (b & (1u << BTN_SEL)) {
                if (mode == M_NEST) {
                    uint8_t t = rowpet[cursor];
                    if (t == 0xFE) go(M_PICK);
                    else if (t == 0xFD) go(M_BAND);
                    else { sv.cur = t; P = &sv.pet[t]; dirty |= 1u; go(M_RAISE); }
                } else if (LIB_READY[cursor]) {
                    int k = new_pet(cursor);
                    if (k >= 0) { sv.cur = (uint8_t)k; P = &sv.pet[k]; dirty |= 1u; go(M_HATCH); }
                }
                if (mode != M_NEST && mode != M_PICK) continue;   /* the new screen draws itself */
            }
            if (redraw) { if (mode == M_NEST) draw_nest(); else draw_pick(); redraw = 0; }
            fm1_led_key(BTN_SEL, (int)(fm1_ms / 180u & 1u));
            continue;
        }
        if (mode == M_HATCH) {
            if (b & (1u << BTN_SEL)) { go(M_RAISE); say_it("An egg! Let's wake it up", 2500); continue; }
            if (redraw) { draw_hatch(); redraw = 0; }
            fm1_led_key(BTN_SEL, (int)(fm1_ms / 180u & 1u));
            continue;
        }

        /* ---- raising and the band: the transport */
        if (b & (1u << BTN_PLAY)) {
            if (playing) { playing = 0; b_held = 0; stop_ms = fm1_ms; if (dirty) save_now(); }
            else { step = 15; samp_left = 0; played = 0; last_seq = bar_seq; playing = 1; }
            redraw = 0xFFu;
        }
        if ((e2 = fm1_enc_take(ENC_KNOB2)) != 0) {
            int32_t v = (int32_t)bpm + e2;
            bpm = (uint32_t)(v < 60 ? 60 : v > 180 ? 180 : v);
            if (mode == M_BAND) sv.band_bpm = (uint8_t)bpm; else P->bpm = (uint8_t)bpm;
            redraw |= 1u; dirty |= 1u;
        }

        if (mode == M_BAND) {
            uint32_t keep = (uint32_t)fm1_enc_take(ENC_KNOB3); (void)keep;
            if (mv) { cursor = (cursor + NLIB + (uint32_t)mv) % NLIB; redraw = 0xFFu; }
            if (e && LIB_READY[cursor]) { band_cycle(cursor, e > 0 ? 1 : -1); redraw = 0xFFu; }
            if (bar_seq != last_seq) {          /* playing in the band feeds them a little */
                last_seq = bar_seq;
                for (l = 0; l < NLIB; l++) if (sv.band[l]) {
                    pet_t *p = &sv.pet[sv.band[l] - 1u];
                    if (p->hunger) p->hunger--;
                    if (p->mood < 100u) p->mood++;
                }
                dirty |= 1u;
            }
            if (step != last_step) { last_step = step; if ((step & 3u) == 0u) { bob = !bob; anim++; redraw |= 8u; } }
            if (redraw) { draw_band(bob); redraw = 0; }
            if (dirty && !playing && fm1_ms - t_save > 30000u) { save_now(); t_save = fm1_ms; }
            for (l = 0; l < 16u; l++) fm1_led_key(KEY_LED(l), playing && l == step);
            fm1_led_key(BTN_PLAY, (int)playing);
            continue;
        }

        /* ---- raising */
        if ((e2 = fm1_enc_take(ENC_KNOB3)) != 0) {
            int32_t v = (int32_t)swing + e2 * 4;
            swing = (uint32_t)(v < 0 ? 0 : v > 128 ? 128 : v); P->swing = (uint8_t)swing; redraw |= 1u; dirty |= 1u;
        }
        if (b & (1u << BTN_SAVE)) { save_now(); say_it("Saved.", 1200); }
        if (b & (1u << BTN_SEQ)) {              /* SEQ clears the lane (bass: the notes); SEL + SEQ the whole pattern */
            int all = (fm1_in.buttons & (1u << BTN_SEL)) != 0;
            if (P->lib == LIB_DRUMS) {
                for (l = 0; l < NLANE; l++) if (all || l == lane) { P->lane[l] = 0; pl_drum[l] = 0; }
            } else { P->gate = 0; pl_bgate = 0; }
            say_it(all || P->lib != LIB_DRUMS ? "Pattern cleared." : "Lane cleared.", 1500);
            redraw |= 4u; dirty |= 1u;
        }
        if (P->lib == LIB_DRUMS) {
            if (e) mv = e > 0 ? 1 : -1;
            if (mv) { lane = (lane + NLANE + (uint32_t)mv) % NLANE; redraw |= 4u; }
            if (down & 0xFFFFu) {
                P->lane[lane] ^= (uint16_t)(down & 0xFFFFu);
                pl_drum[lane] = P->lane[lane];
                if (!playing) trig_req |= 1u << lane;
                redraw |= 4u; dirty |= 1u;
            }
            if (down & (7u << 16)) trig_req |= (down >> 16) & 7u;
        } else {                                /* bass */
            uint32_t root = 36u + (uint32_t)((int32_t)(int8_t)P->oct * 12);
            if (mv) { int32_t o = (int8_t)P->oct + mv; P->oct = (uint8_t)(o < -1 ? -1 : o > 1 ? 1 : o); cur_note = 36u + (uint32_t)((int32_t)(int8_t)P->oct * 12) + cur_note % 12u; redraw |= 4u; }
            if (e) { int32_t v = (int32_t)cur_note + e; cur_note = (uint32_t)(v < 24 ? 24 : v > 60 ? 60 : v); b_req_note = cur_note; trig_req |= 16u; redraw |= 4u; }
            if (down >> 16) {                   /* a note key: A4.. = root + 0..10 */
                uint32_t k = 0;
                while (!(down >> (16u + k) & 1u)) k++;
                cur_note = root + k;
                b_req_note = cur_note; trig_req |= 16u;
                if (keys & 0xFFFFu) {           /* step keys held: they take the note */
                    for (l = 0; l < 16u; l++) if (keys >> l & 1u) { P->note[l] = (uint8_t)cur_note; P->gate |= (uint16_t)(1u << l); }
                    down &= ~(keys & 0xFFFFu);
                    dirty |= 1u;
                }
                redraw |= 4u;
            }
            if (down & 0xFFFFu) {
                for (l = 0; l < 16u; l++) if (down >> l & 1u) {
                    if ((P->gate >> l & 1u) && P->note[l] == cur_note) P->gate &= (uint16_t)~(1u << l);
                    else if (P->gate >> l & 1u) { P->note[l] = (uint8_t)cur_note; if (!playing) { b_req_note = cur_note; trig_req |= 16u; } }
                    else { P->gate |= (uint16_t)(1u << l); P->note[l] = (uint8_t)cur_note; if (!playing) { b_req_note = cur_note; trig_req |= 16u; } }
                }
                redraw |= 4u; dirty |= 1u;
            }
            pl_bgate = P->gate;
            for (l = 0; l < 16u; l++) pl_bnote[l] = P->note[l];
        }

        if (bar_seq != last_seq) { last_seq = bar_seq; eat_bar(); redraw |= 3u; }
        while (fm1_ms - t_sec >= 1000u) { t_sec += 1000u; tick_second(); }
        if ((dirty & 2u) || (dirty && !playing && fm1_ms - t_save > 30000u)) { save_now(); t_save = fm1_ms; }
        if (say_until && fm1_ms >= say_until) { say_until = 0; redraw |= 2u; }
        if (anim_of(P) && !playing && fm1_ms - t_anim > 600u) { t_anim = fm1_ms; anim++; redraw |= 8u; }
        if (step != last_step) {
            last_step = step;
            if (playing && (step & 3u) == 0u) { bob = !bob; anim++; redraw |= 8u; }
            redraw |= 4u;
        }
        {
            static uint32_t icons_was;
            uint32_t now_icons = (fm1_ms < feed_until) | (uint32_t)(fm1_ms < happy_until) << 1 | (uint32_t)(fm1_ms < cheer_until) << 2
                               | (uint32_t)asleep() << 3 | (uint32_t)(P->mood >= 70u) << 4 | (uint32_t)(P->hunger > 70u || P->mood < 30u) << 5;
            if (now_icons != icons_was) { icons_was = now_icons; redraw |= 9u; }
        }
        {
            const char *gt = guide_txt;
            uint16_t gb = guide_bits;
            uint32_t gn = guide_note;
            compute_guide();
            if (gt != guide_txt) redraw |= 2u;
            if (gb != guide_bits || gn != guide_note) redraw |= 4u;
        }
        if (redraw & 1u) draw_top();
        if (redraw & 9u) draw_pet(bob);
        if (redraw & 2u) draw_words();
        if (redraw & 4u) draw_grid();
        redraw = 0;
        {
            int blink = (int)(fm1_ms / 180u & 1u);
            for (l = 0; l < 16u; l++) {
                int on = (int)((P->lib == LIB_DRUMS ? P->lane[lane] : P->gate) >> l & 1u);
                if (playing && l == step) on = !on;
                if (guide_bits >> l & 1u) on = blink;
                fm1_led_key(KEY_LED(l), on);
            }
            fm1_led_key(BTN_OCT_DOWN, guide_btn == BTN_OCT_DOWN && blink);
            fm1_led_key(BTN_OCT_UP, guide_btn == BTN_OCT_UP && blink);
        }
        for (l = 0; l < 11u; l++)
            fm1_led_key(KEY_LED(16u + l), P->lib == LIB_BASS && (guide_note != 0xFFu ? (l == guide_note && (fm1_ms / 180u & 1u))
                                          : cur_note == 36u + (uint32_t)((int32_t)(int8_t)P->oct * 12) + l));
        fm1_led_key(BTN_PLAY, guide_btn == BTN_PLAY ? (int)(fm1_ms / 180u & 1u) : (int)playing);
    }
}
