/* SPDX-License-Identifier: GPL-3.0-only */
/* CLOCK PROBE: does the FM-1 keep time while it is switched off?
 * Built on Dinghy (zvenson), on Felucca's platform (Leo Kuroshita / Hugelton Instruments) as SLOOP carries it.
 *
 * Once a second it reads the chip's power-management bus (P33) and its RTC domain (R3), addresses 0x00..0xFF of each,
 * read only apart from one latch bit the stock firmware also sets before it reads its timer. Registers that keep
 * changing while the FM-1 runs are counters. Every 5 s it saves what it read, with the uptime, to flash. At the next
 * power-on it compares: if a counter moved on while the FM-1 was off, the FM-1 keeps time.
 *
 *   the screen  boot number, uptime, the counters found, and per counter: LAST (saved before power-off),
 *               BOOT (read at this power-on), NOW; then the off-gap the busiest counter reads as, in seconds
 *   HOME        forget the saved record (start a new test)
 *
 * Silent: no sound. Integers only, no 64-bit division (docs/RULES.md). */
#define APP_NAME "CLKPROBE"
#define STORE_MAGIC 0x4B4C4350u                 /* "PCLK": its own record, never another firmware's */
static void app_idle(void);
#define LCD_IDLE app_idle
#include "platform.c"

static void app_idle(void) {}
static void app_audio(int32_t *out, uint32_t n) { (void)out; (void)n; }

/* ---- the bus. On the host a fake: R3 0x40..0x43 a 1 Hz counter that keeps running "while off". */
#define NREG 512u                               /* 0..255: P33, 256..511: R3 (0x8000 | a) */
#ifdef DINGHY_HOST
static uint32_t host_rtc_base = 1000000u;
static uint8_t bus_read(uint32_t i)
{
    uint32_t s = host_rtc_base + fm1_ms / 1000u;
    if (i >= 256u + 0x40u && i <= 256u + 0x43u) return (uint8_t)(s >> (8u * (i - 256u - 0x40u)));
    if (i == 0x44u) return (uint8_t)(fm1_ms >> 3);   /* a fast one that restarts at power-on */
    return (uint8_t)(i * 7u);
}
static void bus_latch(void) {}
static void host_seed(void);                    /* a record from a "previous run", 600 s before this one */
#else
static uint8_t bus_read(uint32_t i) { return fm1_p33_read(i < 256u ? i : 0x8000u | (i - 256u)); }
static void bus_latch(void) { fm1_p33_or(0x060u, 0x02u); }  /* as the stock firmware does before it reads 0x44.. */
#endif

/* ---- the record in flash */
static struct rec {
    uint32_t boots, up_s, saves;
    uint8_t snap[NREG];
} last, cur;
static int have_last;
static uint8_t bootv[NREG], prevv[NREG], nowv[NREG];
static uint16_t chg[NREG];
static uint32_t vlast[2u * NREG];             /* each candidate counter's last value */
static uint8_t mono[2u * NREG];                /* 1 while it has never gone backwards */

static void scan(uint8_t *dst)
{
    uint32_t i;
    bus_latch();
    for (i = 0; i < NREG; i++) {
        dst[i] = bus_read(i);
        if ((i & 63u) == 63u) platform_poll();
    }
}

/* ---- text */
static char *put(char *p, const char *s) { while (*s) *p++ = *s++; *p = 0; return p; }
static char *put_u(char *p, uint32_t v)
{
    char t[12];
    int n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (n) *p++ = t[--n];
    *p = 0;
    return p;
}
static char *put_h(char *p, uint32_t v, int digits)
{
    static const char H[] = "0123456789ABCDEF";
    while (digits--) *p++ = H[(v >> (4 * digits)) & 15u];
    *p = 0;
    return p;
}
static char *put_reg(char *p, uint32_t i) { p = put(p, i < 256u ? "P  " : "R3 "); return put_h(p, i & 255u, 2); }

static void line(uint32_t row, const char *s, uint16_t c) { draw_text_box(0, row * 16u, 240, &FONT_S, s, c, 0); }

#define P_DIM RGB(120, 120, 120)
#define P_HI  RGB(255, 200, 60)
#define P_OK  RGB(90, 220, 120)

/* a 32-bit counter with its low byte at register i, the next bytes going up (dir 1) or down (dir -1) */
static uint32_t val(const uint8_t *v, uint32_t i, int dir)
{
    uint32_t x = 0, k, lo = i < 256u ? 0u : 256u;
    for (k = 0; k < 4u; k++) {
        int32_t j = (int32_t)i + dir * (int32_t)k;
        if (j < (int32_t)lo || j >= (int32_t)lo + 256) break;
        x |= (uint32_t)v[j] << (8u * k);
    }
    return x;
}

static void draw(uint32_t up_s, uint32_t since_save)
{
    char b[48], *p;
    uint32_t i, rows = 0, best = NREG, bestn = 0, row;
    int dir;

    p = put(b, "CLOCK PROBE  boot #"); put_u(p, cur.boots);
    line(0, b, P_HI);
    p = put(b, "up "); p = put_u(p, up_s); p = put(p, " s");
    if (have_last) { p = put(p, "   last run "); p = put_u(p, last.up_s); put(p, " s"); }
    line(1, b, C_WHITE);
#ifndef DINGHY_HOST
    { int32_t a = fm1_adc_read(FM1_ADC_BATT); p = put(b, "battery raw "); if (a >= 0) put_u(p, (uint32_t)a); else put(p, "-"); }
#else
    put(b, "battery raw (host)");
#endif
    line(2, b, P_DIM);
    line(3, "reg    LAST BOOT NOW   n", P_DIM);

    for (i = 0; i < NREG; i++)                  /* the most active register: a counter's low byte */
        if (chg[i] > bestn) { bestn = chg[i]; best = i; }
    for (i = 0; i < NREG && rows < 7u; i++) {   /* the counters: registers that changed at least twice */
        if (chg[i] < 2u) continue;
        p = put_reg(b, i);
        p = put(p, "  ");
        if (have_last) p = put_h(p, last.snap[i], 2); else p = put(p, "--");
        p = put(p, "   "); p = put_h(p, bootv[i], 2);
        p = put(p, "   "); p = put_h(p, prevv[i], 2);
        p = put(p, "  "); put_u(p, chg[i]);
        line(4u + rows++, b, i == best ? P_HI : C_WHITE);
    }
    if (!rows) line(4u + rows++, up_s < 3u ? "looking..." : "no register changes yet", P_DIM);
    while (rows < 7u) line(4u + rows++, "", C_WHITE);

    row = 11;                                   /* the verdict: every steady 32-bit counter, both byte orders */
    if (!have_last || up_s < 20u) {
        line(row++, have_last ? "verdict after 20 s of uptime" : "now: power off, wait, power on", P_DIM);
    } else {
        /* Per register, the byte order that reads as a clock that ran on wins: the wrong order glues unrelated
         * registers above the low byte, and a counter that restarted at power-on reads as RESET in both. */
        for (i = 0; i < NREG && row < 13u; i++) {
            uint32_t rate = 0, gap = 0, found = 0, ran = 0;
            for (dir = 1; dir >= -1; dir -= 2) {
                uint32_t k = 2u * i + (dir < 0), vb, vn, vl, r;
                if (!mono[k] || chg[i] + 2u < up_s) continue;   /* its low byte moves every second, never back */
                vb = val(bootv, i, dir); vn = val(prevv, i, dir); vl = val(last.snap, i, dir);
                if (!(r = (vn - vb) / up_s)) continue;
                if (vb >= vl && !ran) { ran = 1; rate = r; gap = (vb - vl) / r; }
                else if (!found) rate = r;
                found = 1;
            }
            if (!found) continue;
            p = put_reg(b, i); p = put(p, "  ");
            p = put_u(p, rate); p = put(p, "/s  ");
            if (!ran) put(p, "RESET");
            else { p = put(p, "off "); p = put_u(p, gap); put(p, " s"); }
            line(row++, b, ran ? P_OK : P_HI);
        }
        if (row == 11u) line(row++, "no steady counter found", P_HI);
    }
    while (row < 13u) line(row++, "", C_WHITE);
    (void)best;
    line(13, "HOME: forget the record", P_DIM);
    p = put(b, "saved "); p = put_u(p, since_save); put(p, " s ago");
    line(14, b, P_DIM);
}

static void app_main(void)
{
    uint32_t t_scan = 0, t_save = 0, t_boot = fm1_ms;

#ifdef DINGHY_HOST
    host_seed();
#endif
    have_last = settings_load(&last, sizeof last) == 0;
    cur.boots = have_last ? last.boots + 1u : 1u;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    scan(bootv);
    memcpy(prevv, bootv, NREG);
    {
        uint32_t i;
        for (i = 0; i < 2u * NREG; i++) { vlast[i] = val(bootv, i >> 1, (i & 1u) ? -1 : 1); mono[i] = 1; }
    }

    for (;;) {
        uint32_t rel, b, ms;
        platform_poll();
        ms = fm1_ms - t_boot;
        b = fm1_input_edges(&rel);
        if (b & (1u << BTN_HOME)) {             /* a new test: nothing to compare with until the next power-on */
            have_last = 0;
            cur.boots = 1;
            memset(&last, 0, sizeof last);
        }
        if (ms - t_scan >= 1000u) {
            uint32_t i;
            t_scan = ms;
            scan(nowv);
            for (i = 0; i < NREG; i++)
                if (nowv[i] != prevv[i]) { if (chg[i] < 9999u) chg[i]++; prevv[i] = nowv[i]; }
            for (i = 0; i < 2u * NREG; i++) {   /* a counter only goes up (a 32-bit wrap: years at 1 Hz) */
                uint32_t v = val(nowv, i >> 1, (i & 1u) ? -1 : 1);
                if (v < vlast[i]) mono[i] = 0;
                vlast[i] = v;
            }
            if (ms - t_save >= 5000u) {          /* an erase every 5 s: ~700 an hour, the sector takes 100,000 */
                t_save = ms;
                cur.up_s = ms / 1000u;
                cur.saves++;
                memcpy(cur.snap, nowv, NREG);
                settings_save(&cur, sizeof cur);
            }
            draw(ms / 1000u, (ms - t_save) / 1000u);
        }
    }
}

#ifdef DINGHY_HOST
static void host_seed(void)
{
    struct rec r;
    uint32_t i, save = host_rtc_base;
    memset(&r, 0, sizeof r);
    host_rtc_base -= 600u;                      /* the previous run, read 600 s ago */
    for (i = 0; i < NREG; i++) r.snap[i] = bus_read(i);
    r.snap[0x44] = 0xF0;                        /* the fast one was high, it restarts at power-on */
    host_rtc_base = getenv("PROBE_RESET") ? 50u : save;   /* PROBE_RESET: a clock that restarted at power-on */
    r.boots = 3; r.up_s = 95; r.saves = 19;
    settings_save(&r, sizeof r);
}
#endif
