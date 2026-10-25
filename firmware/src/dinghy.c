/* SPDX-License-Identifier: GPL-3.0-only */
/* TAMISYNTH: a pet that lives in the M-VAVE FM-1 and eats the beats you make.
 * Built on Dinghy (zvenson), on Felucca's platform (Leo Kuroshita / Hugelton Instruments) as SLOOP carries it.
 *
 *   keys F3..G#4  the 16 steps of the selected lane (on / off); their LEDs show the lane, the playhead blinks
 *   keys A4 A#4 B4  play kick, snare, hat live
 *   OCT- OCT+     the lane: KICK, SNARE, HAT (KNOB1 too)
 *   KNOB2         tempo 60..180      KNOB3  swing 0..50 %
 *   PLAY          start / stop       SAVE   save the pet now
 *   SEL + HOME    a new egg (press HOME again to confirm)
 *
 * Every bar the pattern plays, the pet eats it. Each stage has a lesson; bars that meet it make it happy and
 * grow it up. What it ate as a teen decides what it becomes: boom bap, house, breaks or dub.
 * It lives on powered-on time (the FM-1 has no clock that runs while off): switched off, it sleeps.
 * Integers only, no 64-bit division (docs/RULES.md). */
#define APP_NAME "TAMISYNTH"
#define STORE_MAGIC 0x494D4154u                 /* "TAMI" */
#define CV_MAX (240u * 120u)                    /* the pet's area in one go */
static void app_idle(void);
#define LCD_IDLE app_idle
#include "platform.c"
#include "sprites.h"

static void app_idle(void) {}

/* ======================================================================================= the sound (audio ISR) */
enum { LK, LS, LH, NLANE };
static volatile uint16_t pat[NLANE];            /* bit i = step i (main loop writes, ISR reads) */
static volatile uint32_t bpm = 100, swing = 0;  /* swing 0..128 = 0..50 % of a step */
static volatile uint32_t playing, step;         /* step: the one sounding now */
static volatile uint32_t trig_req;              /* bits LK..LH: play now (live keys); bit 3: the pet chirps */
static volatile uint32_t bar_seq;               /* + 1 at the end of every bar ... */
static volatile uint16_t bar_pat[NLANE];        /* ... with the pattern that bar played */
static uint32_t samp_left, played;            /* played: a step has sounded since PLAY */

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
static uint32_t k_ph, k_inc, k_amp, s_ph, s_tamp, s_namp, h_amp, c_ph, c_inc, c_amp;
static int32_t h_prev;

static void trig(uint32_t lane)
{
    if (lane == LK) { k_ph = 0; k_inc = HZ(165); k_amp = 26000; }
    else if (lane == LS) { s_ph = 0; s_tamp = 20000; s_namp = 22000; }
    else if (lane == LH) h_amp = 15000;
    else { c_ph = 0; c_inc = HZ(900); c_amp = 9000; }
}

static void app_audio(int32_t *out, uint32_t n)
{
    uint32_t i, r = trig_req;
    if (r) {
        trig_req = 0;
        for (i = 0; i < 4u; i++) if (r >> i & 1u) trig(i);
    }
    for (i = 0; i < n; i++) {
        int32_t s = 0, nn;
        if (playing) {
            if (samp_left == 0) {               /* the next step */
                uint32_t base = 661500u / bpm, st, l;   /* samples a 16th: 44100 * 15 / bpm */
                st = (step + 1u) & 15u;
                if (st == 0 && played) {
                    bar_pat[0] = pat[0]; bar_pat[1] = pat[1]; bar_pat[2] = pat[2];
                    bar_seq++;
                }
                step = st;
                played = 1;
                for (l = 0; l < NLANE; l++) if (pat[l] >> st & 1u) trig(l);
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
        if (c_amp > 40u) {                      /* the pet's chirp: a square rising */
            c_ph += c_inc;
            c_inc += c_inc >> 11;
            s += (int32_t)c_ph < 0 ? (int32_t)c_amp : -(int32_t)c_amp;
            c_amp -= c_amp >> 10;
        }
        s = s * 5 >> 3;                         /* headroom: kick and snare land together on 2 and 4 */
        out[2u * i] += s;
        out[2u * i + 1u] += s;
    }
}

/* ======================================================================================= the pet */
enum { ST_EGG, ST_BABY, ST_CHILD, ST_TEEN, ST_ADULT };
enum { F_BOOMBAP, F_HOUSE, F_BREAKS, F_DUB };
#define BEATS 0x1111u                           /* steps 1 5 9 13: the four beats */
#define BACK  0x1010u                           /* steps 5 13: beats 2 and 4 */
#define OFF8  0x4444u                           /* steps 3 7 11 15: the "and"s */
static const uint32_t NEED[4] = {4, 12, 16, 24};   /* good bars to grow out of each stage */

static struct pet {
    uint32_t version, stage, form, age_s, hunger, mood, prog, bars;
    uint32_t diet[4];
    uint16_t pat[NLANE], pad;
    uint32_t bpm, swing;
} pet;
#define PET_VERSION 1u

static uint32_t pc16(uint32_t v) { uint32_t c = 0; v &= 0xFFFFu; while (v) { v &= v - 1u; c++; } return c; }

static uint32_t classify(const uint16_t *b)     /* what kind of bar this was */
{
    uint32_t hits = pc16(b[LK]) + pc16(b[LS]) + pc16(b[LH]);
    if (hits <= 6u) return F_DUB;
    if ((b[LK] & BEATS) == BEATS && pc16(b[LH] & OFF8) >= 2u) return F_HOUSE;
    if (pc16(b[LS]) >= 4u || pc16(b[LK] & ~BEATS) >= 2u) return F_BREAKS;
    return F_BOOMBAP;
}

static int lesson_ok(const uint16_t *b)
{
    switch (pet.stage) {
    case ST_EGG:   return (b[LK] & BEATS) == BEATS;
    case ST_BABY:  return (b[LK] & BEATS) == BEATS && pc16(b[LH]) >= 2u;
    case ST_CHILD: return (b[LS] & BACK) == BACK && (b[LK] & 1u) && pc16(b[LH]) >= 4u;
    case ST_TEEN:  return (b[LS] & BACK) == BACK && ((b[LK] & ~BEATS & 0xFFFFu) || pet.swing >= 32u);
    default:       return classify(b) == pet.form;
    }
}

static const char *const LESSON[5] = {
    "Hatch me: kick every beat", "4 kicks + some hats", "Snare on 2 and 4 (5,13)",
    "Kick off the beat, or swing", ""};
static const char *const ADULT_TIP[4] = {
    "Boom bap: swing it, lay back", "House: 4 kicks, offbeat hats", "Breaks: busy snares, odd kicks",
    "Dub: less is more"};
static const char *const FORM_NAME[4] = {"CRATE", "FOURFLOOR", "CHOPPER", "ECHO"};
static const char *const STAGE_NAME[4] = {"EGG", "BLIP", "KIK", "HATLING"};

static char say[32];                            /* the pet's latest line */
static uint32_t say_until, dirty, need_full;

static void put(char *d, const char *s) { while ((*d++ = *s++) != 0) {} }
static void say_it(const char *s, uint32_t ms) { put(say, s); say_until = fm1_ms + ms; need_full |= 2u; }

static void new_egg(void)
{
    memset(&pet, 0, sizeof pet);
    pet.version = PET_VERSION;
    pet.mood = 50;
    pet.bpm = 100;
    pet.pat[LK] = 0x0001u;                       /* one kick to start from */
    dirty = 1;
}

static void eat_bar(const uint16_t *b)
{
    uint32_t hits = pc16(b[LK]) + pc16(b[LS]) + pc16(b[LH]), ok = lesson_ok(b);
    pet.bars++;
    if (pet.stage != ST_EGG && hits >= 3u) {     /* food */
        pet.hunger = pet.hunger > 3u ? pet.hunger - 3u : 0u;
        if (ok) pet.hunger = pet.hunger ? pet.hunger - 1u : 0u;
    }
    if (pet.stage == ST_TEEN && hits >= 4u) pet.diet[classify(b)]++;
    if (ok) {
        pet.mood = pet.mood + 4u > 100u ? 100u : pet.mood + 4u;
        if (pet.stage < ST_ADULT) pet.prog++;
        trig_req |= 8u;
        say_it(pet.stage == ST_EGG ? "*tap tap* (it moved!)" : "Yum! That's the one", 1800);
    } else {
        if (pet.mood) pet.mood--;
        if (hits < 3u && pet.stage != ST_EGG) say_it("...is that all?", 1800);
    }
    if (pet.stage < ST_ADULT && pet.prog >= NEED[pet.stage]) {   /* grow */
        pet.stage++;
        pet.prog = 0;
        if (pet.stage == ST_ADULT) {
            uint32_t f, best = 0;
            for (f = 1; f < 4u; f++) if (pet.diet[f] > pet.diet[best]) best = f;
            pet.form = best;
        }
        say_it(pet.stage == ST_BABY ? "It hatched!" : "It grew!", 4000);
        need_full = 15u;
        dirty |= 2u;                            /* save now, even while playing */
    }
    dirty |= 1u;
}

static void tick_second(void)                   /* life goes on while it is switched on */
{
    pet.age_s++;
    if (pet.stage != ST_EGG && pet.age_s % 15u == 0u) {
        if (pet.hunger < 100u) pet.hunger++;
        if (pet.hunger > 70u && pet.mood) pet.mood--;
        if (pet.hunger == 80u) say_it("I'm hungry. Play me a beat", 3000);
        dirty |= 1u;
    }
}

/* ======================================================================================= the screen */
#define C_BG    RGB(28, 31, 43)
#define C_TXT   RGB(230, 230, 236)
#define C_MUTE  RGB(130, 134, 150)
#define C_TEAL  RGB(79, 195, 181)
#define C_HOT   RGB(255, 107, 107)
#define C_GOLD  RGB(242, 193, 78)
static const uint16_t LANE_COL[NLANE] = {RGB(255, 140, 90), RGB(242, 193, 78), RGB(127, 209, 198)};
static uint32_t lane;

static const sprite_t *pet_sprite(void)
{
    static const uint8_t KID[4] = {SP_EGG, SP_BLIP, SP_KIK, SP_HATLING};
    static const uint8_t ADULT[4] = {SP_CRATE, SP_FOURFLOOR, SP_CHOPPER, SP_ECHO};
    return &SPRITES[pet.stage < ST_ADULT ? KID[pet.stage] : ADULT[pet.form & 3u]];
}

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

static void bar(int32_t x, int32_t y, int32_t w, uint32_t v, uint16_t c)   /* v 0..100 */
{
    cv_rect(x, y, w, 6, RGB(55, 60, 78));
    cv_rect(x, y, (int32_t)((uint32_t)w * v / 100u), 6, c);
}

static void draw_top(void)                      /* 0..40: name, age, hunger, mood */
{
    char b[40], *p;
    cv_begin(240, 40, C_BG);
    cat(b, pet.stage < ST_ADULT ? STAGE_NAME[pet.stage] : FORM_NAME[pet.form & 3u]);
    cv_text(6, 2, &FONT_S, b, C_TXT);
    p = put_u(b, pet.age_s / 60u); p = cat(p, "m ");
    p = put_u(p, pet.bpm); p = cat(p, "bpm sw"); p = put_u(p, pet.swing * 100u / 256u); cat(p, "%");
    cv_text(234 - text_w(&FONT_S, b), 2, &FONT_S, b, C_MUTE);
    if (pet.stage != ST_EGG) {
        cv_text(6, 21, &FONT_S, "FED", C_MUTE);
        bar(38, 27, 74, 100u - pet.hunger, pet.hunger > 70u ? C_HOT : C_TEAL);
        cv_text(124, 21, &FONT_S, "JOY", C_MUTE);
        bar(158, 27, 74, pet.mood, pet.mood < 30u ? C_HOT : C_GOLD);
    } else {
        cv_text(6, 21, &FONT_S, "warm it with a pulse", C_MUTE);
    }
    cv_blit(0, 0);
}

static void draw_pet(int bob)                   /* 40..160: the pet, 3x */
{
    const sprite_t *sp = pet_sprite();
    uint32_t x, y;
    int32_t ox = 60, oy = 0 - (bob ? 3 : 0);
    cv_begin(240, 120, C_BG);
    cv_rect(40, 112, 160, 3, RGB(45, 50, 68));   /* the floor */
    for (y = 0; y < SPR_N; y++)
        for (x = 0; x < SPR_N; x++) {
            uint8_t i = sp->px[y * SPR_N + x];
            if (i) cv_rect(ox + (int32_t)x * 3, oy + (int32_t)y * 3, 3, 3, sp->pal[i]);
        }
    if (!playing) cv_text(184, 10, &FONT_S, "z z", C_MUTE);
    cv_blit(0, 40);
}

static void draw_words(void)                    /* 160..192: the lesson (or the pet's line) and the progress */
{
    cv_begin(240, 32, C_BG);
    if (fm1_ms < say_until) cv_text(6, 0, &FONT_S, say, C_GOLD);
    else cv_text(6, 0, &FONT_S, pet.stage < ST_ADULT ? LESSON[pet.stage] : ADULT_TIP[pet.form & 3u], C_TXT);
    if (pet.stage < ST_ADULT) {
        cv_rect(6, 22, 228, 4, RGB(55, 60, 78));
        cv_rect(6, 22, (int32_t)(228u * pet.prog / NEED[pet.stage]), 4, C_TEAL);
    }
    cv_blit(0, 160);
}

static void draw_grid(void)                     /* 192..240: the three lanes, 16 steps */
{
    uint32_t l, s;
    cv_begin(240, 48, C_BG);
    for (l = 0; l < NLANE; l++) {
        int32_t y = 2 + (int32_t)l * 15;
        cv_rect(4, y, 24, 12, l == lane ? LANE_COL[l] : RGB(45, 50, 68));
        cv_text(8, y - 3, &FONT_S, l == 0 ? "K" : l == 1 ? "S" : "H", l == lane ? C_BG : C_MUTE);
        for (s = 0; s < 16u; s++) {
            int32_t x = 32 + (int32_t)s * 13 + (int32_t)(s / 4u) * 1;
            uint16_t c = pat[l] >> s & 1u ? LANE_COL[l] : (s % 4u == 0 ? RGB(62, 68, 90) : RGB(45, 50, 68));
            if (playing && s == step) c = pat[l] >> s & 1u ? C_WHITE : RGB(90, 96, 120);
            cv_rect(x, y, 11, 12, c);
        }
    }
    cv_blit(0, 192);
}

/* ======================================================================================= main */
static void save_now(void)
{
    pet.pat[LK] = pat[LK]; pet.pat[LS] = pat[LS]; pet.pat[LH] = pat[LH];
    pet.bpm = bpm; pet.swing = swing;
    settings_save(&pet, sizeof pet);
    dirty = 0;
}

static void app_main(void)
{
    uint32_t last_step = 99, last_seq, t_sec = fm1_ms, t_save = fm1_ms, confirm_new = 0, prev_keys = 0, l;
    int bob = 0;

    if (settings_load(&pet, sizeof pet) || pet.version != PET_VERSION) new_egg();
    pat[LK] = pet.pat[LK]; pat[LS] = pet.pat[LS]; pat[LH] = pet.pat[LH];
    bpm = pet.bpm >= 60u && pet.bpm <= 180u ? pet.bpm : 100u;
    swing = pet.swing <= 128u ? pet.swing : 0u;
    last_seq = bar_seq;
    lcd_fill(0, 0, 240, 240, C_BG);
    say_it(pet.stage == ST_EGG ? "An egg. It's warm." : "*yawn* ...hi!", 2500);
    need_full = 15u;

    for (;;) {
        uint32_t rel, b, keys, down;
        int32_t e;
        platform_poll();

        /* ---- the panel */
        b = fm1_input_edges(&rel);
        if (b & (1u << BTN_PLAY)) {
            if (playing) { playing = 0; save_now(); }
            else { step = 15; samp_left = 0; played = 0; last_seq = bar_seq; playing = 1; }
            need_full |= 1u;
        }
        if (b & (1u << BTN_OCT_DOWN)) { lane = (lane + NLANE - 1u) % NLANE; need_full |= 4u; }
        if (b & (1u << BTN_OCT_UP)) { lane = (lane + 1u) % NLANE; need_full |= 4u; }
        if (b & (1u << BTN_SAVE)) { save_now(); say_it("Saved.", 1200); }
        if (b & (1u << BTN_HOME)) {
            if (confirm_new && fm1_ms - confirm_new < 4000u) {
                playing = 0; new_egg();
                pat[LK] = pet.pat[LK]; pat[LS] = 0; pat[LH] = 0; bpm = 100; swing = 0;
                save_now(); confirm_new = 0;
                say_it("A new egg.", 2500); need_full = 15u;
            } else if (fm1_in.buttons & (1u << BTN_SEL)) {
                confirm_new = fm1_ms;
                say_it("HOME again: new egg", 4000);
            }
        }
        if ((e = fm1_enc_take(ENC_KNOB1)) != 0) { lane = (uint32_t)((int32_t)lane + (e > 0 ? 1 : -1) + NLANE) % NLANE; need_full |= 4u; }
        if ((e = fm1_enc_take(ENC_KNOB2)) != 0) {
            int32_t v = (int32_t)bpm + e;
            bpm = (uint32_t)(v < 60 ? 60 : v > 180 ? 180 : v); pet.bpm = bpm; need_full |= 1u; dirty |= 1u;
        }
        if ((e = fm1_enc_take(ENC_KNOB3)) != 0) {
            int32_t v = (int32_t)swing + e * 4;
            swing = (uint32_t)(v < 0 ? 0 : v > 128 ? 128 : v); pet.swing = swing; need_full |= 1u; dirty |= 1u;
        }
        keys = fm1_in.notes;
        down = (keys & ~prev_keys) | fm1_input_note_edges();
        prev_keys = keys;
        if (down & 0xFFFFu) {                   /* steps */
            pat[lane] ^= (uint16_t)(down & 0xFFFFu);
            pet.pat[lane] = pat[lane];
            if (!playing) trig_req |= 1u << lane;
            need_full |= 4u; dirty |= 1u;
        }
        if (down & (7u << 16)) trig_req |= (down >> 16) & 7u;   /* live pads */

        /* ---- life */
        if (bar_seq != last_seq) {
            uint16_t bp[NLANE];
            last_seq = bar_seq;
            bp[0] = bar_pat[0]; bp[1] = bar_pat[1]; bp[2] = bar_pat[2];
            eat_bar(bp);
            need_full |= 3u;
        }
        while (fm1_ms - t_sec >= 1000u) { t_sec += 1000u; tick_second(); if (pet.age_s % 10u == 0u) need_full |= 1u; }
        if ((dirty & 2u) || (dirty && !playing && fm1_ms - t_save > 30000u)) { save_now(); t_save = fm1_ms; }
        if (say_until && fm1_ms >= say_until) { say_until = 0; need_full |= 2u; }

        /* ---- the screen and LEDs */
        if (step != last_step) {
            last_step = step;
            if (playing && (step & 3u) == 0u) { bob = !bob; need_full |= 8u; }
            need_full |= 4u;
        }
        if (need_full & 1u) draw_top();
        if (need_full & 8u || need_full & 1u) draw_pet(bob);
        if (need_full & 2u) draw_words();
        if (need_full & 4u) draw_grid();
        need_full = 0;
        for (l = 0; l < 16u; l++) {
            int on = (int)(pat[lane] >> l & 1u);
            if (playing && l == step) on = !on;
            fm1_led_key(KEY_LED(l), on);
        }
        fm1_led_key(BTN_PLAY, (int)playing);
    }
}
