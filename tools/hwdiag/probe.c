/* Video register probe: writes the game's video settings and reads them back at several moments, to see which writes stick on
 * the machine it runs on. Text only. Output: one row per moment, one column per register. */
#include <stdint.h>
#include "platform.h"

static uint16_t tpos;
static void tclear(void)
{
    uint16_t i;
    for (i = 0; i < 2000; ++i) { POKE(0x0800 + i, 32); POKE(0xD800 + i, 1); }
    tpos = 0;
}
static void tc(char c)
{
    if (c == '\n') { tpos = (tpos / 80 + 1) * 80; return; }
    if (c >= 'a' && c <= 'z') c = c - 'a' + 1;
    if (tpos < 2000) POKE(0x0800 + tpos, (uint8_t)c);
    ++tpos;
}
static void ts(const char *p) { while (*p) tc(*p++); }
static void th(uint8_t v)
{
    uint8_t d = v >> 4, e = v & 15;
    tc(d < 10 ? '0' + d : 'a' + d - 10); tc(e < 10 ? '0' + e : 'a' + e - 10);
}

#define NREG 12
static const uint16_t reg[NREG] = { 0xD031, 0xD054, 0xD05A, 0xD05E, 0xD07B, 0xD04C, 0xD04E, 0xD048, 0xD04A, 0xD04B, 0xD06F, 0xD05D };
static uint8_t snap[16][NREG];
static uint8_t when[16];
static uint8_t ns;

static void waitn(uint8_t n) { while (n--) wait_frame(); }

static void snapshot(uint8_t label)
{
    uint8_t i;
    for (i = 0; i < NREG; ++i) snap[ns][i] = PEEK(reg[i]);
    when[ns++] = label;
}

static void game_regs(void)
{
    POKE(0xD031, PEEK(0xD031) | 0x88);
    POKE(0xD054, 0x05);
    POKE(0xD05A, 0x1E);
    POKE(0xD05E, 0x10);
    POKE(0xD058, 0x20); POKE(0xD059, 0);
    POKE(0xD07B, 0x39);
    POKE(0xD04C, 0x90);
    POKE(0xD04E, 0x04);
    POKE(0xD048, 0x04);
    POKE(0xD04A, 0xD4); POKE(0xD04B, 0x01);
}

int main(void)
{
    uint8_t i, k, s0[NREG];
    POKE(0xD02F, 0x47); POKE(0xD02F, 0x53);
    snapshot(0x00);
    POKE(0xD05D, PEEK(0xD05D) & 0x7F);                /* HOTREG off: the chip no longer recomputes the layout when VIC-II registers are touched */                                   /* as found */
    for (i = 0; i < NREG; ++i) s0[i] = snap[0][i];
    game_regs();                  snapshot(0x10);     /* just written */
    waitn(1);                     snapshot(0x11);     /* one frame later */
    waitn(10);                    snapshot(0x1A);
    POKE(0xD06F, PEEK(0xD06F) | 0x80);                /* the 60 Hz switch */
    snapshot(0x20);
    waitn(1);                     snapshot(0x21);
    waitn(10);                    snapshot(0x2A);
    waitn(60);                    snapshot(0x3C);
    game_regs();                  snapshot(0x40);     /* written again after the switch */
    waitn(1);                     snapshot(0x41);
    waitn(10);                    snapshot(0x4A);
    waitn(60);                    snapshot(0x5C);
    /* put the chip back as found, so that the text screen is readable */
    POKE(0xD054, s0[1]); POKE(0xD05A, s0[2]); POKE(0xD05E, s0[3]); POKE(0xD07B, s0[4]);
    POKE(0xD04C, s0[5]); POKE(0xD04E, s0[6]); POKE(0xD048, s0[7]); POKE(0xD04A, s0[8]); POKE(0xD04B, s0[9]);
    POKE(0xD031, s0[0]); POKE(0xD058, s0[11]); POKE(0xD059, 0); POKE(0xD070, 0); POKE(0xD030, 0);
    POKE(0xD020, 0); POKE(0xD021, 6);
    tclear();
    ts("probe  d031 d054 d05a d05e d07b d04c d04e d048 d04a d04b d06f d05d\n");
    for (k = 0; k < ns; ++k) {
        th(when[k]); ts("     ");
        for (i = 0; i < NREG; ++i) { th(snap[k][i]); ts("   "); }
        tc('\n');
    }
    ts("\n10 = after the game's writes, 11/1a = 1/10 frames later\n20-3c = after the 60hz switch (0, 1, 10, 70 frames)\n40-5c = written again after the switch\n");
#ifdef TEST_EXIT
    POKE(0xD6CF, 0x42);
#endif
    for (;;) { }
}
