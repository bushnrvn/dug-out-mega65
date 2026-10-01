/* Dug Out for the MEGA65. */
#include <stdint.h>
#include "platform.h"
#include "game.h"
#include "render.h"

#ifndef TEST_LEVEL
#define TEST_LEVEL 1
#endif
#ifndef TEST_LIVES
#define TEST_LIVES 3
#endif
#ifndef TEST_TICKS
#define TEST_TICKS 240
#endif

extern uint16_t frames_seen;

#ifdef TEST_SCRIPT
#include "testscript.h"      /* const uint8_t script[][2] = { {buttons, ticks}, ... {0, 0} }, written by the test run */
static uint8_t sc_i, sc_left;
static uint8_t script_input(void)
{
    uint8_t m;
    if (!sc_left) { sc_left = script[sc_i][1]; if (!sc_left) sc_left = 255; }
    m = script[sc_i][0];
    if (--sc_left == 0 && script[sc_i][1]) ++sc_i;
    return m;
}
#endif

unsigned char new_best;
static unsigned int hi_at_start_h;
static unsigned char hi_at_start_t;

/* a new game from the title screen */
static void end_run(uint8_t to)                         /* game over or victory */
{
    new_best = (hi_h > hi_at_start_h) || (hi_h == hi_at_start_h && hi_t > hi_at_start_t);
    state = to; state_timer = 0;
}

static void new_game(void)
{
    level = 1; lives = 3; score_h = 0; score_t = 0; next_life_h = 300;
    hi_at_start_h = hi_h; hi_at_start_t = hi_t; new_best = 0;
#ifndef TEST_EXIT
    run_seed = (timer_now() ^ ((uint16_t)frame_ct << 8)) | 1u;      /* new caves every game */
#endif
    score_dirty = 1;
    build_level();
    state = ST_PLAY; state_timer = 0;
}

int main(void)
{
#ifdef TEST_EXIT
    uint16_t n;
#endif
    uint8_t in, old = 0;
    __asm__("sei");                                  /* the ROM's interrupt routine would fight over the screen */
    POKE(0x00, 65);                                  /* 40 MHz */
    timer_start();                                   /* a free-running clock: paces the game and seeds the caves */

    level = TEST_LEVEL; lives = TEST_LIVES; next_life_h = 300;
#if defined(TEST_EXIT) && !defined(TEST_TITLE)
    state = ST_PLAY;                                 /* test builds go straight into the game, unless asked for the title */
#else
    state = ST_TITLE;                                /* the real game starts at the title screen */
#endif
#ifdef TEST_EXIT
    run_seed = 4661u;                                /* the same caves every test run */
#else
    run_seed = ((uint16_t)PEEK(0xDC05) << 8 | PEEK(0xDC04)) | 1u;     /* a timer that has been running since power-on */
#endif
#ifdef TEST_START
    score_h = 107; score_t = 0; hi_h = 107; hi_t = 0; new_best = 1; state = TEST_START;      /* a screenshot of the game over or victory screen */
#endif
    build_level();
    render_init();
    render_frame(); render_frame();

#ifdef TEST_EXIT
    for (n = 0; n < TEST_TICKS; ++n) {
#else
    for (;;) {
#endif
#ifdef TEST_EXIT
#ifdef TEST_SCRIPT
        in = script_input();
#elif defined(TEST_DIG)
        in = (n < 60) ? INPUT_MASK_DOWN : (n < 120) ? INPUT_MASK_LEFT : (n < 180) ? INPUT_MASK_DOWN : INPUT_MASK_RIGHT;
#else
        in = 0;                                      /* Doug stands still; the enemies walk */
#endif
#else
        in = read_input();
#endif
        player1_buttons = in;
        player1_new_buttons = in & ~old;
        old = in;
        ++frame_ct;
        if (state == ST_TITLE) {
            if (player1_new_buttons & (INPUT_MASK_START | INPUT_MASK_A)) new_game();
        } else if (state == ST_OVER || state == ST_WIN) {
            if (state_timer < 255) ++state_timer;
            if (state_timer >= 20 && (player1_new_buttons & (INPUT_MASK_START | INPUT_MASK_A))) state = ST_TITLE;
        } else if (state == ST_PAUSE) {
            if (player1_new_buttons & INPUT_MASK_START) state = ST_PLAY;
        } else if (state == ST_PLAY) {
            play_update();
        } else if (state == ST_DYING) {
            if (++state_timer > 40) {
                state_timer = 0;
                if (lives) --lives;
                if (!lives) end_run(ST_OVER);
                else { reset_round(); state = ST_PLAY; }
            }
        } else if (state == ST_CLEAR) {
            if (++state_timer > 60) {
                state_timer = 0;
                ++level;
                if (level > INNINGS) end_run(ST_WIN);
                else { build_level(); state = ST_PLAY; }
            }
        }
        render_frame();                              /* waits for the next frame itself, so ticks are 2 frames apart */
    }
#ifdef TEST_EXIT
    score_h = frames_seen; ++frame_ct;          /* the frame count shows up as the score */
    render_frame();
    POKE(0xD6CF, 0x42);                              /* Xemu in -testing mode exits when this is written */
    for (;;) { }
#endif
}
