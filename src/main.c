/* Dug Out for the MEGA65. */
#include <stdint.h>
#include "platform.h"
#include "game.h"
#include "render.h"
#include "sound.h"
#ifdef HW_DIAG
#include "hwdiag.h"
#endif

#ifndef TEST_LEVEL
#define TEST_LEVEL 1
#endif
#ifndef TEST_DIST
#define TEST_DIST 24
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

unsigned char test_done;                                  /* test builds: set when the run finished normally (not by the watchdog) */
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
    level = 1; lives = TEST_LIVES; score_h = 0; score_t = 0; next_life_h = 100;
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

#ifdef TEST_BOT
/* A simple player for the test builds: presses Start at the menus, and in play digs toward the nearest enemy and throws when
 * it is lined up. It writes every change of state, inning or lives into a small log that the test run dumps at the end. */
unsigned int trace[64][3];                                /* tick, state, (inning << 4) | lives */
unsigned char trace_n;
static unsigned int bot_tick;
static unsigned int play_ticks;
static int absi(int v) { return v < 0 ? -v : v; }

static uint8_t bot_input(void)
{
    uint8_t i, best = 255;
    int bd = 9999, d, dx, dy;
    uint8_t in = 0;
    if (state == ST_TITLE || state == ST_OVER || state == ST_WIN) return (frame_ct & 1) ? INPUT_MASK_START : 0;
    if (state == ST_INTRO) return (state_timer > 31 && (frame_ct & 1)) ? INPUT_MASK_START : 0;
    if (state != ST_PLAY) { play_ticks = 0; return 0; }
#ifdef TEST_CHEAT
    if (++play_ticks > TEST_CHEAT) {                      /* after a while of real fighting, clear the inning the quick way */
        for (i = 0; i < MAXE; ++i) e_state[i] = ES_NONE;
        enemies_left = 0;
        return 0;
    }
#endif
    for (i = 0; i < MAXE; ++i) {
        if (e_state[i] == ES_NONE) continue;
        d = absi((int)e_x[i] - px) + absi((int)e_y[i] - py);
        if (d < bd) { bd = d; best = i; }
    }
    if (best == 255) return 0;
    dx = (int)e_x[best] - px; dy = (int)e_y[best] - py;
    if (absi(dy) < 5 && absi(dx) < 72) {                  /* in the same row: face it and throw */
        uint8_t want = (dx > 0) ? DIR_R : DIR_L;
        if (pdir == want && !ball_on && !throw_cd) return INPUT_MASK_A;
        return (dx > 0) ? INPUT_MASK_RIGHT : INPUT_MASK_LEFT;
    }
    if (absi(dx) < 5 && absi(dy) < 72) {                  /* in the same column */
        uint8_t want = (dy > 0) ? DIR_D : DIR_U;
        if (pdir == want && !ball_on && !throw_cd) return INPUT_MASK_A;
        return (dy > 0) ? INPUT_MASK_DOWN : INPUT_MASK_UP;
    }
    /* otherwise dig toward it, along whichever way is further, changing the choice now and then */
    if ((absi(dx) > absi(dy)) ^ ((bot_tick >> 6) & 1)) in = (dx > 0) ? INPUT_MASK_RIGHT : INPUT_MASK_LEFT;
    else in = (dy > 0) ? INPUT_MASK_DOWN : INPUT_MASK_UP;
    return in;
}

static void bot_log(void)
{
    static unsigned char ls = 255, ll, lv;
    ++bot_tick;
    if (state != ls || level != ll || lives != lv) {
        ls = state; ll = level; lv = lives;
        if (trace_n < 64) {
            trace[trace_n][0] = bot_tick; trace[trace_n][1] = state; trace[trace_n][2] = ((unsigned int)level << 4) | lives;
            ++trace_n;
        }
    }
}
#endif

int main(void)
{
#ifdef TEST_EXIT
    uint16_t n;
#endif
    uint8_t in, old = 0;
    __asm__("sei");                                  /* the ROM's interrupt routine would fight over the screen */
    POKE(0x00, 65);                                  /* 40 MHz */
    timer_start();                                   /* a free-running clock: paces the game and seeds the caves */

    level = TEST_LEVEL; lives = TEST_LIVES; next_life_h = 100;
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
    score_h = 107; score_t = 0; hi_h = 107; hi_t = 0; new_best = 1; state = TEST_START; icur = intro_for_level(level);      /* a screenshot of the game over or victory screen */
#endif
    build_level();
#ifdef TEST_REVIVE
    e_state[0] = ES_NONE;                            /* Vumpire 0 is out, its headstone lies under Vumpire 1: the revival should start */
    c_on[0] = 1; c_slot[0] = 0; c_x[0] = e_x[1]; c_y[0] = e_y[1];
    enemies_left = 2;
#ifdef TEST_STOMP
    px = c_x[0]; py = c_y[0];                        /* Doug stands on the headstone: he should stomp it, and nothing is raised */
#endif
#ifdef TEST_ALONE
    e_state[1] = ES_NONE; e_state[2] = ES_NONE;      /* no living Vumpire left to raise anyone: the headstone should go */
    enemies_left = 0;
#endif
#endif
#ifdef TEST_FLEE
    enemies_left = 2; score_h = 10;                  /* two left and 1,000 points: enemy 0 has an open way to the top and should run it, costing its value; the sealed ones stay put */
    e_x[0] = 6 << 3; e_y[0] = 3 << 3; M(6, 3) = 0; px = 10 << 3; py = 2 << 3;      /* enemy 0 stands just below Doug's shaft, with an open way to the surface; Doug is moved aside */
#endif
#ifdef TEST_GOLD
    gold_c = 6; gold_r = 2;                        /* the gold bar lies where Doug starts: picking it up should score 500 */
#endif
#ifdef TEST_HEATER
    {   /* inning 2: Doug in a cleared row, TEST_DIST pixels from the Heater (enemy 1), facing it, with nothing else about */
        unsigned char hc = e_x[1] >> 3, hr = e_y[1] >> 3, c;
        e_state[0] = ES_NONE; enemies_left = 1;
        if (e_x[1] >= TEST_DIST) { px = e_x[1] - TEST_DIST; pdir = DIR_R; for (c = (px >> 3); c <= hc; ++c) M(c, hr) = 0; }
        else                     { px = e_x[1] + TEST_DIST; pdir = DIR_L; for (c = hc; c <= (px >> 3); ++c) M(c, hr) = 0; }
        py = e_y[1];
    }
#endif
#ifdef TEST_BAT
    {   /* inning 3: only the baseball bat (the last enemy) is left; Doug stands still */
        unsigned char k;
        for (k = 0; k < MAXE; ++k) if (e_state[k] != ES_NONE && e_type[k] != 2) e_state[k] = ES_NONE;
        enemies_left = 1;
    }
#endif
#ifdef TEST_GK
    {   /* inning 5: only the Groundskeeper; every cell of his pocket looks dug by Doug for 40 points, and Doug has 10,000 */
        unsigned char k, c, r = 0;
        for (k = 0; k < MAXE; ++k) { if (e_state[k] != ES_NONE && e_type[k] != 3) e_state[k] = ES_NONE; else if (e_type[k] == 3) r = e_homer[k]; }
        enemies_left = 1;
        for (c = 0; c < COLS; ++c) if (M(c, r) == 0) paid[(r << 4) | c] = 4;
        score_h = 100; score_t = 0; score_dirty = 1;
    }
#endif
#ifdef TEST_BOSS
#ifndef TEST_BOSS_DIST
#define TEST_BOSS_DIST 32
#endif
    {   /* a straight, cleared tunnel from Doug to the boss, 32 pixels long, Doug facing him: a fair throwing fight */
        unsigned char bc = e_x[0] >> 3, br = e_y[0] >> 3, c;
        if (e_x[0] >= TEST_BOSS_DIST) { px = e_x[0] - TEST_BOSS_DIST; pdir = DIR_R; for (c = (px >> 3); c <= bc; ++c) M(c, br) = 0; }
        else              { px = e_x[0] + TEST_BOSS_DIST; pdir = DIR_L; for (c = bc; c <= (px >> 3); ++c) M(c, br) = 0; }
        py = e_y[0];
#ifdef TEST_BOSS_STRIKES
        e_infl[0] = TEST_BOSS_STRIKES;               /* the boss already has this many strikes on him */
#endif
    }
#endif
    render_init();
#ifdef TEST_AFTER_INIT
    POKE(0xD6CF, 0x42);
    for (;;) { }
#endif
    snd_start();
    load_hiscore();
#ifndef TEST_EXIT
    snd_song(SONG_TITLE, 1);
#elif defined(TEST_MUSIC)
    snd_song(TEST_MUSIC, 1);
#endif
    render_frame(); render_frame();
    render_show();
#ifdef HW_DIAG
    hw_diag_post();
#endif
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
#ifdef TEST_BOT
        in = bot_input();
#elif defined(TEST_SCRIPT)
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
#ifdef TEST_BOT
        bot_log();
        if ((state == ST_OVER || state == ST_WIN) && scene_t > 120) n = TEST_TICKS;      /* the run is over: stop */
#endif
        render_frame();                              /* waits for the next frame itself, so ticks are 2 frames apart */
    }
#ifdef TEST_EXIT
++frame_ct;
    render_frame();
    POKE(0xD6CF, 0x42);                              /* Xemu in -testing mode exits when this is written */
    for (;;) { }
#endif
}
