/* Dug Out for the MEGA65. */
#include <stdint.h>
#include "platform.h"
#include "game.h"
#include "render.h"
#include "sound.h"

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

/* the sound data is in attic RAM (loaded at start-up); the player reads it from chip RAM */
static void snd_start(void)
{
    dma_copy28(0x80, 0x40000UL, 0, 0x1A000UL, 16384);
    snd_init();
}

/* a new game from the title screen */
/* the best score lives in a small file on the disk (src/early.s loads it at start-up and saves it) */
extern unsigned char hs_buf[5];
void save_hiscore(void);

static void load_hiscore(void)
{
    if (hs_buf[0] == 0x44 && hs_buf[1] == 0x4F) {
        hi_h = (unsigned int)hs_buf[2] | ((unsigned int)hs_buf[3] << 8);
        hi_t = hs_buf[4];
    }
}

static void save_hiscore_if_new(void)
{
#if !defined(TEST_EXIT) || defined(TEST_SAVE)
    hs_buf[0] = 0x44; hs_buf[1] = 0x4F;
    hs_buf[2] = (unsigned char)hi_h; hs_buf[3] = (unsigned char)(hi_h >> 8); hs_buf[4] = hi_t;
    save_hiscore();
    timer_start();                                   /* the KERNAL's disk code reprograms the CIA timer the pacing uses */
#endif
}

unsigned int scene_t;                                     /* ticks on the game over, victory and attract screens */
uint8_t icur;                                             /* which enemy the introduction screen is about (0-4) */
static unsigned int idle_t;                               /* ticks at the title with no button pressed */

/* Every time an inning brings a new kind of enemy, a short screen introduces it. */
static uint8_t intro_for_level(uint8_t lv)
{
    switch (lv) {
    case 1: return 0;
    case 2: return 1;
    case 3: return 2;
    case 5: return 3;
    case INNINGS: return 4;
    }
    return 255;
}
static char ready_text[9] = "INNING 0";
static uint8_t paused_shown;                              /* ticks on the game over and victory screens */

static void end_run(uint8_t to)                         /* game over or victory */
{
    state = to; state_timer = 0; scene_t = 0;
    if (to == ST_WIN) add_score(10 * lives);                 /* 1,000 per life left */
    new_best = (hi_h > hi_at_start_h) || (hi_h == hi_at_start_h && hi_t > hi_at_start_t);
    if (new_best) save_hiscore_if_new();
    if (to == ST_OVER) snd_song(SONG_OVER, 0);
    else snd_song(SONG_TITLE, 1);
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
    icur = intro_for_level(1);
    state = ST_INTRO; state_timer = 0;
    snd_stop();
    snd_sfx(SFX_START, 2);
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
    snd_start();
    load_hiscore();
#ifndef TEST_EXIT
    snd_song(SONG_TITLE, 1);
#elif defined(TEST_MUSIC)
    snd_song(TEST_MUSIC, 1);
#endif
    render_frame(); render_frame();
#ifdef TEST_ENDRUN
    score_h = 4321; score_t = 3; hi_h = 4321; hi_t = 3; hi_at_start_h = 0; hi_at_start_t = 0;
    end_run(ST_OVER);                                /* a run that ends at once with a new best score: tests saving it */
#endif

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
            if (player1_buttons) idle_t = 0; else ++idle_t;
            if (player1_new_buttons & (INPUT_MASK_START | INPUT_MASK_A)) new_game();
            else if (idle_t > 450) { state = ST_ATTRACT; scene_t = 0; }       /* about 15 s of nothing: show the cast */
        } else if (state == ST_ATTRACT) {
            ++scene_t;
            if (player1_new_buttons & (INPUT_MASK_START | INPUT_MASK_A)) new_game();
            else if (player1_new_buttons || scene_t > 520) { state = ST_TITLE; idle_t = 0; }
        } else if (state == ST_INTRO) {
            if (++state_timer > 230 || (state_timer > 30 && (player1_new_buttons & (INPUT_MASK_START | INPUT_MASK_A)))) {
                state = ST_READY; state_timer = 0;
            }
        } else if (state == ST_OVER || state == ST_WIN) {
            if (scene_t < 60000u) ++scene_t;
            state_timer = (scene_t > 255) ? 255 : (unsigned char)scene_t;
            if ((state == ST_OVER && scene_t > 600) || (scene_t > 20 && (player1_new_buttons & (INPUT_MASK_START | INPUT_MASK_A)))) {
                state = ST_TITLE; idle_t = 0; snd_song(SONG_TITLE, 1);
            }
        } else if (state == ST_READY) {                      /* "INNING n / PLAY BALL!", then the music starts and play begins */
            if (state_timer == 21) {
                snd_sfx(SFX_READY, 2);
                if (level >= INNINGS) render_banner("FINAL INNING", "BOSS: MAD SCOTT");
                else {
                    ready_text[7] = '0' + level;
                    render_banner(ready_text, "PLAY BALL!");
                }
            }
            if (++state_timer > 90) {
                render_banner_off();
                state = ST_PLAY; state_timer = 0;
                snd_song(SONG_THEME, 1);
            }
        } else if (state == ST_PAUSE) {
            if (!paused_shown) { render_banner("PAUSED", 0); paused_shown = 1; }
            if (player1_new_buttons & INPUT_MASK_START) { render_banner_off(); paused_shown = 0; state = ST_PLAY; }
        } else if (state == ST_PLAY) {
            play_update();
        } else if (state == ST_DYING) {
            rocks_update();                                  /* a falling boulder keeps falling */
            if (++state_timer > 70) {
                state_timer = 0;
                if (lives) --lives;
                if (!lives) end_run(ST_OVER);
                else { reset_round(); state = ST_READY; }
            }
        } else if (state == ST_CLEAR) {
            if (state_timer == 0) render_banner("INNING OVER!", 0);
            if (++state_timer > 130) {
                state_timer = 0;
                render_banner_off();
                ++level;
                if (level > INNINGS) end_run(ST_WIN);
                else {
                    build_level();
                    icur = intro_for_level(level);
                    if (icur != 255) { state = ST_INTRO; snd_sfx(SFX_START, 2); }
                    else state = ST_READY;
                }
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
