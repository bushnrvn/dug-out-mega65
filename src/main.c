/* Dug Out for the MEGA65. */
#include <stdint.h>
#include "platform.h"
#include "game.h"
#include "render.h"

#ifndef TEST_TICKS
#define TEST_TICKS 240
#endif

int main(void)
{
#ifdef TEST_EXIT
    uint16_t n;
#endif
    uint8_t in, old = 0;
    __asm__("sei");                                  /* the ROM's interrupt routine would fight over the screen */
    POKE(0x00, 65);                                  /* 40 MHz */

    level = 1; lives = 3; state = ST_PLAY;
#ifdef TEST_EXIT
    run_seed = 4661u;                                /* the same caves every test run */
#else
    run_seed = ((uint16_t)PEEK(0xDC05) << 8 | PEEK(0xDC04)) | 1u;     /* a timer that has been running since power-on */
#endif
    build_level();
    render_init();
    render_frame(); render_frame();

#ifdef TEST_EXIT
    for (n = 0; n < TEST_TICKS; ++n) {
#else
    for (;;) {
#endif
        wait_frame();                                /* about 25 ticks a second on PAL */
#ifdef TEST_EXIT
        in = 0;                                      /* Doug stands still; the enemies walk */
#else
        in = read_input();
#endif
        player1_buttons = in;
        player1_new_buttons = in & ~old;
        old = in;
        ++frame_ct;
        if (state == ST_PLAY) {
            play_update();
        } else if (state == ST_DYING) {
            if (++state_timer > 40) {
                state_timer = 0;
                if (lives) --lives;
                reset_round();
                state = ST_PLAY;
            }
        } else if (state == ST_CLEAR) {
            if (++state_timer > 60) {
                state_timer = 0;
                ++level;
                build_level();
                state = ST_PLAY;
            }
        }
        render_frame();                              /* waits for the next frame itself, so ticks are 2 frames apart */
    }
#ifdef TEST_EXIT
    POKE(0xD6CF, 0x42);                              /* Xemu in -testing mode exits when this is written */
    for (;;) { }
#endif
}
