/* SID probe: plays one voice at a time on each of the four SIDs, labelled on screen, and reads each chip's voice-3 oscillator to
 * see whether the chip is running. Listen, and note which tests are silent. */
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
static void waitn(uint16_t n) { while (n--) wait_frame(); }

static const uint16_t base[4] = { 0xD400, 0xD420, 0xD440, 0xD460 };
static const char *const sidname[4] = { "sid 1 d400", "sid 2 d420", "sid 3 d440", "sid 4 d460" };

static void silence(void)
{
    uint8_t s, i;
    for (s = 0; s < 4; ++s) for (i = 0; i < 0x19; ++i) POKE(base[s] + i, 0);
}

int main(void)
{
    uint8_t s, v, o1, o2, line = 0;
    __asm__("sei");
    POKE(0x00, 65);
    POKE(0xD02F, 0x47); POKE(0xD02F, 0x53);
    tclear();
    ts("sid probe: listen. each voice plays 1 second.\n\n");
    for (;;) {
        for (s = 0; s < 4; ++s) {
            for (v = 0; v < 3; ++v) {
                uint16_t b = base[s] + v * 7;
                silence();
                POKE(base[s] + 0x18, 0x0F);                      /* volume */
                POKE(b + 5, 0x00); POKE(b + 6, 0xF0);            /* attack 0, decay 0, sustain 15 */
                POKE(b + 3, 0x08);                               /* pulse width */
                POKE(b + 1, 0x20 + v * 0x10); POKE(b, 0x00);     /* a note, a different pitch per voice */
                POKE(b + 4, v == 0 ? 0x41 : v == 1 ? 0x21 : 0x21);   /* pulse / saw, gate on */
                tpos = (3 + s * 3 + v) * 80;
                ts(sidname[s]); ts(" voice "); th(v + 1); ts(" playing       ");
                if (v == 2) {                                    /* voice 3: its oscillator can be read back */
                    waitn(10);
                    o1 = PEEK(base[s] + 0x1B); waitn(1); o2 = PEEK(base[s] + 0x1B);
                    ts(" osc3 "); th(o1); tc(' '); th(o2); ts(o1 != o2 ? " running" : " not running");
                }
                waitn(60);
                POKE(b + 4, 0);
                tpos = (3 + s * 3 + v) * 80 + 22;
                ts("done");
            }
        }
        silence();
        waitn(120);
        tpos = 20 * 80; ts("again...");
    }
}
