/* Sound and input probe: starts the sound player and shows, live, whether its interrupt runs and what the keyboard and
 * joystick registers read. Press cursor keys, Z, Return, and move the joystick (port 2, then port 1) to see what changes. */
#include <stdint.h>
#include "platform.h"
#include "sound.h"

extern volatile uint8_t snd_log[16];

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
static void kv(const char *k, uint8_t v) { ts(k); tc(' '); th(v); ts("  "); }

int main(void)
{
    uint8_t c, jb0, jb1, rr;
    uint16_t n = 0;
    __asm__("sei");
    POKE(0x00, 65);
    tclear();
    POKE(0xD02F, 0x47); POKE(0xD02F, 0x53);
    POKE(0xD06F, PEEK(0xD06F) | 0x80);               /* the same 60 Hz switch as the game */
    wait_frame(); wait_frame(); wait_frame();
    timer_start();
    dma_copy28(0x80, 0x40000UL, 0, 0x1A000UL, 16384);
    snd_init();
    snd_song(SONG_TITLE, 1);
    for (;;) {
        wait_frame(); ++n;
        tpos = 0;                                    /* rewrite in place: a full clear takes longer than a frame */
        ts("sound + input probe\n\n");
        kv("irq count", snd_log[15]); kv("d012", PEEK(0xD012)); kv("d019", PEEK(0xD019)); kv("d01a", PEEK(0xD01A)); tc('\n');
        kv("fffe", PEEK(0xFFFE)); kv("ffff", PEEK(0xFFFF)); kv("0314", PEEK(0x0314)); kv("0315", PEEK(0x0315)); kv("d030", PEEK(0xD030)); tc('\n');
        kv("d06f", PEEK(0xD06F)); kv("d41b", PEEK(0xD41B)); kv("d418", PEEK(0xD418)); kv("frames", (uint8_t)n); tc('\n');
        tc('\n'); ts("keyboard matrix, dc01 per column (ff = nothing down):\n");
        POKE(0xDC02, 0xFF); POKE(0xDC03, 0x00);
        for (c = 0; c < 8; ++c) {
            POKE(0xDC00, (uint8_t)~(1u << c));
            ts("col "); th(c); tc(' '); th(PEEK(0xDC01)); ts("   ");
            if (c == 3) tc('\n');
        }
        tc('\n');
        POKE(0xDC00, 0xFF); POKE(0xDC02, 0x00);
        jb0 = PEEK(0xDC00); jb1 = PEEK(0xDC01);
        tc('\n'); ts("with dc02=00 (joystick reads): dc00 "); th(jb0); ts(" dc01 "); th(jb1); tc('\n');
        rr = read_input();
        ts("game read_input() "); th(rr); ts("   (1 up 2 down 4 left 8 right 10 z 20 return)\n");
        ts("d610 (ascii key) "); th(PEEK(0xD610)); ts("  d611 "); th(PEEK(0xD611)); tc('\n');
        if ((n & 63) == 0) POKE(0xD610, 0);
    }
}
