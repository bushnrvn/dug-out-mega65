/*
 * Dug Out game logic, shared with the GameTank version: the map, Doug, the enemies and their behaviour, the boulders,
 * strikes and scoring. Everything that touches the hardware (drawing, sound, input) is outside this file: the few
 * places the logic needs it are the macros and variables just below.
 */
#include <stdint.h>
#include "game.h"

/* sound: the names are the GameTank version's, mapped onto the SID player (sound.h) */
#include "sound.h"
#define SFXP(id, pri) snd_sfx(id, pri)
#define SFX(id) snd_sfx(id, 0)
#define play_song(a, b) snd_song(a, b)
#define stop_music() snd_stop()
#define REPEAT_NONE 0
#define ASSET__audio__clear_mid SONG_CLEAR
#define ASSET__audio__die_sfx_ID SFX_DIE
#define ASSET__audio__dig_sfx_ID SFX_DIG
#define ASSET__audio__fall_sfx_ID SFX_FALL
#define ASSET__audio__flame_sfx_ID SFX_FLAME
#define ASSET__audio__oneup_sfx_ID SFX_ONEUP
#define ASSET__audio__pop_sfx_ID SFX_POP
#define ASSET__audio__pump1_sfx_ID SFX_PUMP1
#define ASSET__audio__pump2_sfx_ID SFX_PUMP2
#define ASSET__audio__ready_sfx_ID SFX_READY
#define ASSET__audio__shoot_sfx_ID SFX_SHOOT
#define ASSET__audio__squash_sfx_ID SFX_SQUASH
#define ASSET__audio__thud_sfx_ID SFX_THUD

/* the drawing layer redraws the field when this is set (a cell was dug, raked shut or smashed) */
uint8_t field_dirty;
#define mark_around(c, r) (field_dirty = 1)
#define mark_restore(c, r) (field_dirty = 1)

#define WINDUP 10           /* frames a Heater stands still before the fire comes out */
#define FLAME_END 30

/* buttons, in the form the logic expects */
int player1_buttons, player1_new_buttons;

static const signed char DX[4] = { 1, -1, 0, 0 };
static const signed char DY[4] = { 0, 0, -1, 1 };
static const unsigned char LEADX[4] = { 8, 0, 4, 4 };
static const unsigned char LEADY[4] = { 4, 4, 0, 8 };
static const signed char ORB_X[8] = { 0, 6, 9, 6, 0, -6, -9, -6 };
static const signed char ORB_Y[8] = { -9, -6, 0, 6, 9, 6, 0, -6 };
#define ORB_K(n) ((unsigned char)(((frame_ct >> 2) + ((n) << 2)) & 7))
static const unsigned int rock_pts[6] = { 10, 25, 40, 60, 80, 100 };   /* hundreds */

unsigned char map[(ROWS + 1) * 16];
unsigned char paid[(ROWS + 1) * 16];
unsigned char state, state_timer, frame_ct;
unsigned char level, lives;
unsigned int score_h, hi_h, next_life_h;
unsigned char score_t, hi_t;
unsigned char score_dirty;
unsigned int lfsr = 0xACE1u;
unsigned int run_seed = 0x1357u;
unsigned char px, py, pdir, panim, pmoving;
unsigned char ball_on, ball_x, ball_y, ball_dir, ball_dist, throw_cd, ball_dirt;
unsigned char e_state[MAXE], e_type[MAXE], e_x[MAXE], e_y[MAXE], e_dir[MAXE], e_face[MAXE];
unsigned char e_infl[MAXE], e_timer[MAXE], e_acc[MAXE], e_homec[MAXE], e_homer[MAXE], e_flen[MAXE];
unsigned char pmv;
unsigned char e_mv[MAXE];
unsigned char e_flee[MAXE], e_pts[MAXE];      /* running for the top (the last two enemies), and what it would cost if they get there (hundreds) */
unsigned char e_prevc[MAXE], e_prevr[MAXE];
unsigned char c_on[MAXC], c_slot[MAXC], c_x[MAXC], c_y[MAXC];
unsigned char enemies_left, espeed;
unsigned char r_on[MAXR], r_c[MAXR], r_r[MAXR], r_y[MAXR], r_state[MAXR], r_timer[MAXR], r_kills[MAXR];
unsigned char pop_t[MAXP], pop_x[MAXP], pop_y[MAXP];
unsigned int pop_v[MAXP];

/* The beat. The theme's snare hits fall on a grid 52.15 frames apart (26.075 game ticks), the first 26.9 frames into the song, and the song
 * (8371 frames) loops. beat_acc is how far into a snare period it is, in 1/256 ticks; bob is set for the first part of each half of it, so the
 * head nods on every snare hit and on the beat between (when the player and the walkers are moving). */
#define BEAT_FP    6675u
#define BEAT_START (6675u - 3443u)
#define BEAT_WIN   1280u
#define SONG_LEN   8371u
static unsigned int beat_acc, song_t;
static unsigned char beat_on;
unsigned char bob;

void beat_start(void) { beat_acc = BEAT_START; song_t = 0; beat_on = 1; }

void beat_tick(void)
{
    if (beat_on) {
        beat_acc += 256; song_t += 2;
        if (beat_acc >= BEAT_FP) beat_acc -= BEAT_FP;
        if (song_t >= SONG_LEN) { song_t -= SONG_LEN; beat_acc = BEAT_START + song_t * 128u; }      /* the song starts again, and so does the grid */
    }
    bob = (beat_on && (state == ST_PLAY || state == ST_PAUSE) && (beat_acc < BEAT_WIN || (beat_acc >= BEAT_FP / 2 && beat_acc < BEAT_FP / 2 + BEAT_WIN)));
}
unsigned char gold_on, gold_c, gold_r;


static void add_popup(unsigned char x, unsigned char y, unsigned int v)
{
    unsigned char i, best = 0, oldest = 255;
    for (i = 0; i < MAXP; ++i)
        if (pop_t[i] == 0) { best = i; break; }
        else if (pop_t[i] < oldest) { oldest = pop_t[i]; best = i; }
    pop_x[best] = x; pop_y[best] = y; pop_v[best] = v; pop_t[best] = 28;
}

static unsigned char rng(void)
{
    unsigned char i;
    for (i = 0; i < 8; ++i) {
        unsigned char lsb = lfsr & 1;
        lfsr >>= 1;
        if (lsb) lfsr ^= 0xB400u;
    }
    return (unsigned char)lfsr;
}


static unsigned char absdiff(unsigned char a, unsigned char b)
{
    return a > b ? a - b : b - a;
}


void add_score(unsigned int h)
{
    score_h += h;
    if (score_h > hi_h || (score_h == hi_h && score_t > hi_t)) { hi_h = score_h; hi_t = score_t; }
    if (score_h >= next_life_h) {
        next_life_h += 100;
        if (lives < 5) ++lives;
        SFXP(ASSET__audio__oneup_sfx_ID, 2);
    }
    score_dirty = 1;
}

static void add_tens(unsigned char t)
{
    score_t += t;
    if (score_t >= 10) { score_t -= 10; add_score(1); }
    else add_score(0);
}


static void clear_map(void)
{
    unsigned char r, c;
    for (r = 0; r < ROWS + 1; ++r)
        for (c = 0; c < 16; ++c)
            { M(c, r) = (r == 0 && c < COLS) ? 0 : 1; paid[(r << 4) | c] = 0; }
}


static void carve(unsigned char c, unsigned char r, unsigned char w, unsigned char h)
{
    unsigned char i, j;
    for (j = 0; j < h; ++j)
        for (i = 0; i < w; ++i)
            M(c + i, r + j) = 0;
}


static void reset_player(void)
{
    px = 6 << 3; py = 2 << 3; pdir = DIR_D; panim = 0; pmoving = 0;
    ball_on = 0; throw_cd = 0;
    carve(6, 1, 1, 2);
}


static void reset_enemies_home(void)
{
    unsigned char i;
    for (i = 0; i < MAXE; ++i) {
        if (e_state[i] == ES_NONE) continue;
        if (e_state[i] == ES_POP || e_state[i] == ES_SQUASH) { e_state[i] = ES_NONE; continue; }     /* already counted out: it stays gone */
        e_state[i] = (e_type[i] == 2) ? ES_GHOST : ES_WALK; e_flee[i] = 0; e_mv[i] = 0;
        e_x[i] = e_homec[i] << 3; e_y[i] = e_homer[i] << 3;
        e_dir[i] = DIR_R; e_face[i] = DIR_R; e_prevc[i] = 255; e_prevr[i] = 255;
        e_infl[i] = 0; e_timer[i] = 0; e_acc[i] = 0;
    }
}


void build_level(void)
{
    unsigned char i, ne, nr, tries, c, r, w, c0;

    clear_map();
    lfsr = run_seed + level * 977u;

    ne = 2 + level;
    if (ne > MAXE) ne = MAXE;
    if (level >= INNINGS) ne = 1;               /* the final inning is the Mascot, alone */

    for (i = 0; i < MAXE; ++i) { e_state[i] = ES_NONE; e_flee[i] = 0; e_mv[i] = 0; }
    for (i = 0; i < MAXC; ++i) c_on[i] = 0;
    for (i = 0; i < ne; ++i) {
        /* each pocket gets its own row, width, column and sometimes a shaft: nothing sits in a fixed slot */
        for (tries = 0; tries < 30; ++tries) {
            r = 3 + rng() % 9;
            w = 2 + rng() % 4;
            if (level >= 2 && (i & 1) && w < 4) w = 4;                  /* a Heater's pocket is at least 4 wide, or you could not get to it */
            c0 = rng() % (COLS - w + 1);
            if (c0 <= 6 && c0 + w > 6 && r < 7) continue;               /* not under Doug's shaft */
            for (c = 0; c < i; ++c)                                     /* keep pockets a row apart if we can */
                if (absdiff(e_homer[c], r) < 2) break;
            if (c == i) break;
        }
        carve(c0, r, w, 1);
        if (r < 11 && (c0 & 1)) carve(c0 + (w >> 1), r, 1, 2);            /* a shaft down */
        e_state[i] = ES_WALK;
        e_type[i] = (level >= 2 && (i & 1)) ? 1 : 0;
        e_homec[i] = c0 + (w >> 1); e_homer[i] = r;
    }
    if (level >= 3) e_type[ne - 1] = 2;          /* a baseball bat joins from inning 3 */
    if (level >= 5 && ne >= 5) e_type[ne - 2] = 3; /* a Groundskeeper from inning 5 */
    if (level >= INNINGS) e_type[0] = 4;
    enemies_left = ne;                          /* every enemy, the Groundskeeper included, must be struck out */

    /* boulders */
    for (i = 0; i < MAXR; ++i) r_on[i] = 0;
    nr = 3 + (level > 2);
    if (nr > MAXR) nr = MAXR;
    for (i = 0; i < nr; ++i) {
        for (tries = 0; tries < 40; ++tries) {
            c = 1 + rng() % 12;
            r = 2 + rng() % 9;
            if (M(c, r) != 1 || M(c, r + 1) != 1) continue;
            if (M(c - 1, r) == 0 || M(c + 1, r) == 0 || M(c, r - 1) == 0) continue;
            if (c == 6 && r < 5) continue;
            r_on[i] = 1; r_c[i] = c; r_r[i] = r; r_y[i] = r << 3;
            r_state[i] = RS_STILL; r_timer[i] = 0; r_kills[i] = 0;
            M(c, r) = 2;
            break;
        }
    }
    i = rng() % ne;                             /* a gold bar lies in one of the enemy caves, near its middle */
    c = e_homec[i] + rng() % 3 - 1; r = e_homer[i];
    if (M(c, r) != 0) c = e_homec[i];
    gold_c = c; gold_r = r; gold_on = 1;
    reset_player();
    reset_enemies_home();
    field_dirty = 1;
    espeed = 8 + level;
    if (espeed > 13) espeed = 13;
}

static unsigned char can_move(unsigned char x, unsigned char y, unsigned char d)
{
    switch (d) {
    case DIR_R:
        if (x >= 104) return 0;
        if (!(x & 7) && M((x >> 3) + 1, y >> 3) == 2) return 0;
        return 1;
    case DIR_L:
        if (x == 0) return 0;
        if (!(x & 7) && M((x >> 3) - 1, y >> 3) == 2) return 0;
        return 1;
    case DIR_U:
        if (y == 0) return 0;
        if (!(y & 7) && M(x >> 3, (y >> 3) - 1) == 2) return 0;
        return 1;
    default:
        if (y >= 96) return 0;
        if (!(y & 7) && M(x >> 3, (y >> 3) + 1) == 2) return 0;
        return 1;
    }
}


static void enemy_pop(unsigned char i)
{
    e_state[i] = ES_POP;
    e_timer[i] = 0;
    SFXP(ASSET__audio__pop_sfx_ID, 3);
}


static unsigned char vumpires_alive(void)
{
    unsigned char k;
    for (k = 0; k < MAXE; ++k)
        if (e_type[k] == 0 && (e_state[k] == ES_WALK || e_state[k] == ES_INFL || e_state[k] == ES_FLAME)) return 1;
    return 0;
}


static void add_corpse(unsigned char k)
{
    unsigned char c;
    for (c = 0; c < MAXC; ++c)
        if (!c_on[c]) { c_on[c] = 1; c_slot[c] = k; c_x[c] = e_x[k]; c_y[c] = e_y[k]; return; }
}

static void strike(unsigned char k)
{
    unsigned char need = (e_type[k] == 4) ? 6 : 3;
    unsigned char row = e_y[k] >> 3;
    unsigned int pts;
    if (e_type[k] != 4) e_state[k] = ES_INFL;   /* everyone but Mad Scott is stunned by a hit */
    e_timer[k] = 0;
    ++e_infl[k];
    if (e_infl[k] >= need) {
        pts = (e_type[k] == 4) ? 50 : row <= 3 ? 2 : row <= 6 ? 3 : row <= 9 ? 4 : 5;
        enemy_pop(k);
        add_score(pts);
        add_popup(e_x[k], e_y[k], pts);
        if (e_type[k] == 0) add_corpse(k);      /* Vumpires can be raised again; crushed ones can't */
        --enemies_left;
    } else {
        SFXP(e_infl[k] == 1 ? ASSET__audio__pump1_sfx_ID : ASSET__audio__pump2_sfx_ID, 1);
    }
}

static void ball_step(void)
{
    unsigned char s, k, over;
    for (s = 0; s < 2 && ball_on; ++s) {
        ball_x += DX[ball_dir] * 2; ball_y += DY[ball_dir] * 2;
        ball_dist += 2;
        over = (ball_dist > 40 || ball_x >= 112 || ball_y >= 104);
        if (over) { ball_on = 0; break; }
        if (M(ball_x >> 3, ball_y >> 3) != 0) ball_dirt = 1;
        for (k = 0; k < MAXE; ++k) {
            if (e_state[k] != ES_WALK && e_state[k] != ES_GHOST && e_state[k] != ES_FLAME && e_state[k] != ES_INFL) continue;
            if (ball_dirt && e_type[k] < 2) continue;
            if (absdiff(e_x[k] + 4, ball_x) < 6 && absdiff(e_y[k] + 4, ball_y) < 6) {
                strike(k);
                ball_on = 0;
                break;
            }
        }
        if (ball_on && ball_dirt) ball_on = 0;      /* the ball stops at dirt (after one last chance to hit a bat or the boss right there) */
    }
}


static void player_update(void)
{
    unsigned char want = 255, misal, d, moved = 0, c, r, slow, pay;
    int b = player1_buttons;

    if (pdir < 2) {
        if (b & INPUT_MASK_UP) want = DIR_U;
        else if (b & INPUT_MASK_DOWN) want = DIR_D;
        else if (b & INPUT_MASK_LEFT) want = DIR_L;
        else if (b & INPUT_MASK_RIGHT) want = DIR_R;
    } else {
        if (b & INPUT_MASK_LEFT) want = DIR_L;
        else if (b & INPUT_MASK_RIGHT) want = DIR_R;
        else if (b & INPUT_MASK_UP) want = DIR_U;
        else if (b & INPUT_MASK_DOWN) want = DIR_D;
    }

    if (want != 255) {
        if ((want >> 1) != (pdir >> 1)) {
            /* turning a corner: slide onto the grid first */
            misal = (want < 2) ? (py & 7) : (px & 7);
            if (misal) {
                if (want < 2) d = (misal <= 4) ? DIR_U : DIR_D;
                else          d = (misal <= 4) ? DIR_L : DIR_R;
                if (can_move(px, py, d)) {
                    px += DX[d]; py += DY[d];
                    moved = 1;
                }
                want = 255;
            } else {
                pdir = want;
            }
        } else {
            pdir = want;
        }
    }

    if (want != 255 && can_move(px, py, pdir)) {
        c = (px + LEADX[pdir]) >> 3;
        r = (py + LEADY[pdir]) >> 3;
        slow = (M(c, r) == 1);
        if (!slow || (frame_ct & 1)) {
            px += DX[pdir]; py += DY[pdir];
            moved = 1;
        }
    }

    pmoving = moved;
    if (moved) pmv = 4; else if (pmv) --pmv;          /* digging is half speed, so Doug moves every other tick: hold "moving" a few ticks for the head bob */
    if (moved) {
        ++panim;
        c = (px + 4) >> 3; r = (py + 4) >> 3;
        if (M(c, r) == 1) {
            M(c, r) = 0;
            pay = (r <= 3) ? 1 : (r <= 6) ? 2 : (r <= 9) ? 3 : 4;    /* 10, 20, 30, 40 points, by depth */
            paid[(r << 4) | c] = pay;
            add_tens(pay);
            mark_around(c, r);
            SFX(ASSET__audio__dig_sfx_ID);
        }
    }

    if (gold_on && ((px + 4) >> 3) == gold_c && ((py + 4) >> 3) == gold_r) {      /* the gold bar: enemies walk over it, Doug picks it up */
        gold_on = 0;
        add_score(5); add_popup(px, py, 5);
        SFXP(ASSET__audio__oneup_sfx_ID, 1);
    }

    /* --- throw a baseball ---------------------------------------------- */
    if (throw_cd) --throw_cd;
    if ((player1_new_buttons & INPUT_MASK_A) && !ball_on && !throw_cd) {
        ball_on = 1; ball_dir = pdir; ball_dist = 0; ball_dirt = 0;
        ball_x = px + 4; ball_y = py + 4;
        throw_cd = 4;
        SFXP(ASSET__audio__shoot_sfx_ID, 1);
    }
    if (ball_on) ball_step();
}


static unsigned char open_cell(signed char c, signed char r)
{
    if (c < 0 || c >= COLS || r < 0 || r >= ROWS) return 0;
    return M(c, r) == 0;
}


static unsigned char bfs_dir(unsigned char i, unsigned char goal);

static void choose_dir(unsigned char i)
{
    signed char c = e_x[i] >> 3, r = e_y[i] >> 3;
    signed char tc = (px + 4) >> 3, tr = (py + 4) >> 3;
    unsigned char cur = e_dir[i], rev = cur ^ 1, d, best = 255, bd = rev, n = 0, dist, opt[4];
    signed char nc, nr, dc, dr;

    if (e_type[i] == 0 && rng() < 110) {          /* a Vumpire: go and raise a fallen friend */
        for (d = 0; d < MAXC; ++d)
            if (c_on[d]) { tc = (c_x[d] + 4) >> 3; tr = (c_y[d] + 4) >> 3; break; }
    }
    if (tc >= 0 && tc < COLS && tr >= 0 && tr < ROWS && rng() < 235 && bfs_dir(i, (unsigned char)((tr << 4) | tc))) return;     /* a way there along the tunnels: take it */
    dist = rng();                                /* no way (or a sudden whim): aim a few cells off Doug so the routes are not the same every time */
    tc += (dist & 7) - 3;
    tr += ((dist >> 3) & 7) - 3;
    for (d = 0; d < 4; ++d) {
        if (d == rev) continue;
        nc = c + DX[d]; nr = r + DY[d];
        if (!open_cell(nc, nr)) continue;
        opt[n++] = d;
        dc = nc - tc; dr = nr - tr;
        if (dc < 0) dc = -dc;
        if (dr < 0) dr = -dr;
        dist = dc + dr;
        if (dist < best) { best = dist; bd = d; }
    }
    if (n == 0) {
        /* dead end: turn around if we can, else stay put */
        nc = c + DX[rev]; nr = r + DY[rev];
        e_dir[i] = open_cell(nc, nr) ? rev : cur;
        return;
    }
    if (n > 1 && rng() < (e_type[i] ? 70 : 50)) bd = opt[rng() % n];      /* mostly they head straight for Doug */
    e_dir[i] = bd;
}


static unsigned char orb_hits_player(unsigned char i)
{
    unsigned char k;
    if (e_type[i] != 1) return 0;
    k = ORB_K(0);                                   /* the one fireball: its box is where it is drawn */
    return (absdiff((unsigned char)(e_x[i] + ORB_X[k]), px) < 5 && absdiff((unsigned char)(e_y[i] + ORB_Y[k]), py) < 5);
}



static unsigned char line_clear(unsigned char c, unsigned char r, unsigned char dir, unsigned char len)
{
    unsigned char k;
    signed char cc = c;
    for (k = 0; k < len; ++k) {
        cc += DX[dir];
        if (!open_cell(cc, r)) return k;
    }
    return len;
}

/* scratch for the two searches over the tunnels: the Groundskeeper's (which caves is it joined to) and the fleeing enemies' (the way to the top) */
static unsigned char fl_par[(ROWS + 1) << 4], fl_q[(ROWS + 1) << 4];

/* mark in fl_par every open cell that is joined to the start cell by open cells */
static void region_mark(unsigned char start)
{
    unsigned char head = 0, tail = 0, cur, d, nc, nr, n;
    for (n = 0; n < sizeof fl_par; ++n) fl_par[n] = 0;
    fl_q[tail++] = start; fl_par[start] = 1;
    while (head != tail) {
        cur = fl_q[head++];
        for (d = 0; d < 4; ++d) {
            nc = (cur & 15) + DX[d]; nr = (cur >> 4) + DY[d];
            if (!open_cell((signed char)nc, (signed char)nr)) continue;
            n = (nr << 4) | nc;
            if (fl_par[n]) continue;
            fl_par[n] = 1; fl_q[tail++] = n;
        }
    }
}

static unsigned char gk_ok(signed char c, signed char r)
{
    if (c < 0 || c >= COLS || r < 0 || r >= ROWS) return 0;
    return M(c, r) != 2;                       /* walks through dirt, but not through home plates */
}


static void choose_dir_g(unsigned char i)
{
    signed char c = e_x[i] >> 3, r = e_y[i] >> 3;
    signed char tc = (px + 4) >> 3, tr = (py + 4) >> 3;
    unsigned char rev = e_dir[i] ^ 1, d, best = 255, bd = rev, n = 0, dist, opt[4], rr, cc, pass, any = 0;
    signed char nc, nr, dc, dr;

    if (e_type[i] == 3) {
        /* Groundskeeper: go straight (digging, see enemy_update) for the nearest open cell that is not joined to where it is, which is
         * a sealed cave, or failing that the nearest open cell. Never the surface. */
        unsigned char bestd = 255;
        region_mark((unsigned char)((r << 4) | c));
        for (pass = 0; pass < 2 && !any; ++pass)
        for (rr = 1; rr < ROWS; ++rr)
            for (cc = 0; cc < COLS; ++cc) {
                if (M(cc, rr) != 0 || (cc == c && rr == r)) continue;
                if (!pass && fl_par[(rr << 4) | cc]) continue;
                dc = cc - c; dr = rr - r;
                if (dc < 0) dc = -dc;
                if (dr < 0) dr = -dr;
                dist = dc + dr;
                if (dist < bestd) { bestd = dist; tc = cc; tr = rr; any = 1; }
            }
    }
    for (d = 0; d < 4; ++d) {
        if (d == rev) continue;
        nc = c + DX[d]; nr = r + DY[d];
        if (!gk_ok(nc, nr)) continue;
        opt[n++] = d;
        dc = nc - tc; dr = nr - tr;
        if (dc < 0) dc = -dc;
        if (dr < 0) dr = -dr;
        dist = dc + dr;
        if (dist < best) { best = dist; bd = d; }
    }
    if (n == 0) { e_dir[i] = rev; return; }
    if (e_type[i] == 4 && n > 1 && rng() < 24) bd = opt[rng() % n];
    e_dir[i] = bd;
}

static void sub_tens(unsigned char t)
{
    if (score_t >= t) score_t -= t;
    else if (score_h) { --score_h; score_t += 10 - t; }
    else score_t = 0;
    score_dirty = 1;
}


static void refill_cell(unsigned char c, unsigned char r, unsigned char self)
{
    unsigned char k;
    if (r == 0 || M(c, r) != 0) return;
    if (paid[(r << 4) | c] - 1u > 3u) return;                 /* it only takes back what Doug dug: never a cave, never a tunnel it dug itself */
    if (absdiff(px, c << 3) < 8 && absdiff(py, r << 3) < 8) return;
    for (k = 0; k < MAXE; ++k) {
        if (k == self || e_state[k] == ES_NONE || e_state[k] == ES_POP || e_state[k] == ES_SQUASH) continue;
        if (absdiff(e_x[k], c << 3) < 8 && absdiff(e_y[k], r << 3) < 8) return;
    }
    M(c, r) = 1;
    k = (r << 4) | c;
    if (paid[k]) { sub_tens(paid[k]); paid[k] = 0; }      /* the Groundskeeper takes back what the dig paid */
    mark_restore(c, r);
    mark_around(c, r);
}


/* an enemy made it off the top of the screen: Doug loses what it would have been worth */
static void enemy_escape(unsigned char i)
{
    if (score_h > e_pts[i]) score_h -= e_pts[i]; else { score_h = 0; score_t = 0; }
    score_dirty = 1;
    add_popup(e_x[i], 0, e_pts[i] | 0x8000);
    SFXP(ASSET__audio__thud_sfx_ID, 2);
    e_state[i] = ES_NONE;
    --enemies_left;
}

/* the way along open tunnels (breadth first search from the enemy's cell to the goal cell, or to any cell of row 0 if goal is 255: the top).
 * Sets e_dir and returns 1, or returns 0 if there is no way: a sealed cave stays sealed. */
static unsigned char bfs_dir(unsigned char i, unsigned char goal)
{
    unsigned char head = 0, tail = 0, cur, c, r, d, nc, nr, n, start, d0 = 0;
    for (n = 0; n < sizeof fl_par; ++n) fl_par[n] = 0;
    start = ((e_y[i] >> 3) << 4) | (e_x[i] >> 3);
    fl_q[tail++] = start; fl_par[start] = 5;
    while (head != tail) {
        cur = fl_q[head++]; c = cur & 15; r = cur >> 4;
        if (goal == 255 ? (r == 0 && cur != start) : (cur == goal && cur != start)) {      /* found it: walk back to the first step */
            while (cur != start) {
                d = fl_par[cur] - 1; d0 = d;
                cur = (unsigned char)(((((signed char)(cur >> 4)) - DY[d]) << 4) | (((signed char)(cur & 15)) - DX[d]));
            }
            e_dir[i] = d0;
            return 1;
        }
        for (d = 0; d < 4; ++d) {
            nc = c + DX[d]; nr = r + DY[d];
            if (!open_cell((signed char)nc, (signed char)nr)) continue;
            n = (nr << 4) | nc;
            if (fl_par[n]) continue;
            fl_par[n] = d + 1;
            fl_q[tail++] = n;
        }
    }
    return 0;
}

static void enemy_update(unsigned char i)
{
    unsigned char st = e_state[i], x = e_x[i], y = e_y[i], d, c, r, tc, tr;

    if (enemies_left <= 2 && e_type[i] <= 2 && !e_flee[i] && (st == ES_WALK || st == ES_GHOST)) {
        /* only two left: they give up the chase and run for the top, along the tunnels if there is a way (bats fly straight up) */
        e_flee[i] = 1;
        r = y >> 3;
        e_pts[i] = (r <= 3) ? 2 : (r <= 6) ? 3 : (r <= 9) ? 4 : 5;      /* what a kill here would pay */
    }

    if (e_mv[i]) --e_mv[i];

    switch (st) {
    case ES_WALK:
        if (e_type[i] == 0 && !e_flee[i]) {
            /* standing over a fallen Vumpire starts the ritual */
            for (c = 0; c < MAXC; ++c)
                if (c_on[c] && e_state[c_slot[c]] == ES_NONE && absdiff(x, c_x[c]) < 6 && absdiff(y, c_y[c]) < 6) {
                    e_state[i] = ES_FLAME; e_timer[i] = 0; e_flen[i] = c;
                    SFXP(ASSET__audio__ready_sfx_ID, 2);
                    return;
                }
        }
        if (e_type[i] >= 3) {
            if (e_type[i] == 4) {
                /* Mad Scott never stops for a hit: he gets faster with every strike, and strikes wear off */
                if (e_infl[i] && ++e_timer[i] > 150) { e_timer[i] = 0; --e_infl[i]; }
                e_acc[i] += 9 + e_infl[i];
            } else {
                e_acc[i] += 14;                               /* the Groundskeeper is brisk */
            }
            if (e_acc[i] < 16) return;
            e_acc[i] -= 16;
            if (!((x | y) & 7)) {
                if (e_type[i] == 3) {
                    /* rake shut the cell behind and any open cell beside the path */
                    if (e_prevc[i] != 255) refill_cell(e_prevc[i], e_prevr[i], i);
                    for (c = 0; c < 4; ++c) {
                        if (c == e_dir[i]) continue;
                        tc = (x >> 3) + DX[c]; tr = (y >> 3) + DY[c];
                        if (tc >= 0 && tc < COLS && tr >= 1 && tr < ROWS && M(tc, tr) == 0) refill_cell(tc, tr, i);
                    }
                }
                e_prevc[i] = x >> 3; e_prevr[i] = y >> 3;
                choose_dir_g(i);
                {
                    /* the Mascot smashes through the dirt ahead of it, leaving a tunnel; so does the Groundskeeper (quietly), which is
                     * how it opens up the sealed caves. Its tunnel is marked in paid[] so that it never rakes it shut. */
                    signed char mc = (x >> 3) + DX[e_dir[i]], mr = (y >> 3) + DY[e_dir[i]];
                    if (mc >= 0 && mc < COLS && mr >= 1 && mr < ROWS && M(mc, mr) == 1) {
                        M(mc, mr) = 0;
                        mark_around(mc, mr);
                        if (e_type[i] == 3) paid[(mr << 4) | mc] = 0x10;
                        else SFX(ASSET__audio__dig_sfx_ID);
                    }
                }
            }
            d = e_dir[i];
            e_x[i] = x + DX[d]; e_y[i] = y + DY[d]; e_mv[i] = 6;
            if (d < 2) e_face[i] = d;
            break;
        }
        e_acc[i] += espeed + (enemies_left == 1 ? 3 : 0);
        if (e_acc[i] < 16) return;
        e_acc[i] -= 16;
        if (!((x | y) & 7)) {
            c = x >> 3; r = y >> 3;
            tc = (px + 4) >> 3; tr = (py + 4) >> 3;
            /* heaters spit fire when lined up with Doug */
            if (e_type[i] == 1 && !e_flee[i] && r == tr && (tc != c) && rng() < 130) {
                d = (tc > c) ? DIR_R : DIR_L;
                if (absdiff(tc, c) <= 5 && line_clear(c, r, d, absdiff(tc, c)) == absdiff(tc, c)) {
                    e_state[i] = ES_FLAME; e_timer[i] = 0; e_face[i] = d;
                    e_flen[i] = line_clear(c, r, d, 3);
                    return;
                }
            }
            if (!(e_flee[i] && bfs_dir(i, 255))) choose_dir(i);
        }
        d = e_dir[i];
        /* enemies only walk through tunnels (choose_dir guarantees the next cell) */
        e_x[i] = x + DX[d]; e_y[i] = y + DY[d]; e_mv[i] = 6;
        if (d < 2) e_face[i] = d;
        if (e_flee[i] && e_y[i] == 0) enemy_escape(i);      /* out through the top of Doug's shaft */
        break;

    case ES_GHOST:
        if (e_timer[i] < 250) ++e_timer[i];
        if (e_flee[i]) {
            e_acc[i] += espeed - 2;                        /* a little slower than Doug, so he can cut them off */
            if (e_acc[i] < 16) return;
            e_acc[i] -= 16;
            if (y) --y;
            e_y[i] = y;
            if (y == 0) enemy_escape(i);
            break;
        }
        if (e_type[i] == 2) {
            /* baseball bat: flies straight at Doug through dirt, diagonally, never lands.
             * It hovers in its pocket for a few seconds (100 ticks) at the start of a round. */
            if (e_timer[i] < 100) return;
            e_acc[i] += 11 + (level >> 1);                 /* bats get quicker in later innings */
            if (e_acc[i] < 16) return;
            e_acc[i] -= 16;
            if (x < px) { ++x; e_face[i] = DIR_R; }
            else if (x > px) { --x; e_face[i] = DIR_L; }
            if (frame_ct & 1) { if (y < py) ++y; else if (y > py) --y; }
            e_x[i] = x; e_y[i] = y;
            break;
        }
        break;

    case ES_INFL:
        ++e_timer[i];
        if (e_timer[i] > ((e_type[i] == 4) ? 120 : 90)) {
            e_timer[i] = 0;
            if (--e_infl[i] == 0) {
                e_state[i] = (e_type[i] == 2) ? ES_GHOST : ES_WALK; e_acc[i] = 0;
            }
        }
        break;

    case ES_POP:
        if (++e_timer[i] > 8) e_state[i] = ES_NONE;
        break;

    case ES_SQUASH:
        if (++e_timer[i] > 24) e_state[i] = ES_NONE;
        break;

    case ES_FLAME:
        if (e_type[i] == 0) {                          /* Vumpire raising a fallen one */
            c = e_flen[i];
            if (!c_on[c]) { e_state[i] = ES_WALK; e_acc[i] = 0; break; }
            if (++e_timer[i] > 45) {
                r = c_slot[c];
                if (e_state[r] == ES_NONE) {
                    e_state[r] = ES_WALK; e_type[r] = 0;
                    e_x[r] = c_x[c]; e_y[r] = c_y[c];
                    e_infl[r] = 0; e_timer[r] = 0; e_acc[r] = 0;
                    ++enemies_left;
                    SFXP(ASSET__audio__oneup_sfx_ID, 2);
                }
                c_on[c] = 0;
                e_state[i] = ES_WALK; e_acc[i] = 0;
            }
            break;
        }
        ++e_timer[i];
        if (e_timer[i] == WINDUP) SFXP(ASSET__audio__flame_sfx_ID, 2);
        if (e_timer[i] > FLAME_END) { e_state[i] = ES_WALK; e_acc[i] = 0; }
        break;
    }
}


static unsigned char flame_hits_player(unsigned char i)
{
    unsigned char len = e_flen[i] * 8;              /* exactly as long as it is drawn */
    unsigned char fx;
    if (e_type[i] != 1 || e_state[i] != ES_FLAME || e_timer[i] < WINDUP) return 0;
    if (absdiff(e_y[i], py) > 5) return 0;          /* the flame's solid rows are 1-6 of its 8; Doug's body is the middle 6x6 of his sprite */
    if (e_face[i] == DIR_R) {
        fx = e_x[i] + 8;
        return (px + 7 > fx && px + 1 < fx + len);
    }
    fx = e_x[i] > len ? e_x[i] - len : 0;
    return (px + 7 > fx && px + 1 < e_x[i]);
}


static void enemies_update_all(void)
{
    unsigned char i;
    for (i = 0; i < MAXE; ++i)
        if (e_state[i] != ES_NONE) enemy_update(i);
}


static unsigned char enemies_touch_player(void)
{
    unsigned char i, k;
    for (i = 0; i < MAXE; ++i) {
        k = e_state[i];
        if ((k == ES_WALK || k == ES_GHOST || k == ES_FLAME || k == ES_INFL) && e_type[i] != 3) {   /* a stunned enemy still kills on touch */
            if (absdiff(e_x[i], px) < 6 && absdiff(e_y[i], py) < 6) return 1;
            if (k != ES_INFL && (flame_hits_player(i) || orb_hits_player(i))) return 1;
        }
    }
    return 0;
}

void rocks_update(void)
{
    unsigned char i, k, c, r, below;
    for (i = 0; i < MAXR; ++i) {
        if (!r_on[i]) continue;
        c = r_c[i]; r = r_r[i];
        switch (r_state[i]) {
        case RS_STILL:
            if (M(c, r + 1) == 0) { r_state[i] = RS_WOBBLE; r_timer[i] = 0; }
            break;
        case RS_WOBBLE:
            if (++r_timer[i] >= 26) {
                r_state[i] = RS_FALL;
                r_kills[i] = 0;
                M(c, r) = 0;
                mark_around(c, r);
                SFXP(ASSET__audio__fall_sfx_ID, 1);
            }
            break;
        case RS_FALL:
            r_y[i] += 2;
            if (!(r_y[i] & 7)) {
                below = (r_y[i] >> 3) + 1;
                r_r[i] = r_y[i] >> 3;
                if (below >= ROWS || M(c, below) != 0) {
                    r_state[i] = RS_CRUMBLE; r_timer[i] = 0;
                    SFXP(ASSET__audio__thud_sfx_ID, 2);
                }
            }
            /* crush things underneath */
            for (k = 0; k < MAXE; ++k) {
                if (e_state[k] == ES_NONE || e_state[k] == ES_POP || e_state[k] == ES_SQUASH) continue;
                if (absdiff(e_x[k], c << 3) < 7 && absdiff(e_y[k], r_y[i]) < 7) {
                    if (e_type[k] == 4) {
                        /* the Mascot is padded: a home plate counts as three strikes, then shatters */
                        e_timer[k] = 0;
                        e_infl[k] += 3;
                        r_state[i] = RS_CRUMBLE; r_timer[i] = 0;
                        SFXP(ASSET__audio__thud_sfx_ID, 3);
                        if (e_infl[k] >= 6) {
                            enemy_pop(k); add_score(50); add_popup(e_x[k], e_y[k], 50);
                            --enemies_left;
                        }
                        break;
                    }
                    e_state[k] = ES_SQUASH; e_timer[k] = 0;
                    e_x[k] = c << 3; e_y[k] = r_y[i] + 3;
                    if (e_y[k] > 96) e_y[k] = 96;
                    add_score(rock_pts[r_kills[i] < 5 ? r_kills[i] : 5]);
                    add_popup(e_x[k], e_y[k] > 8 ? e_y[k] - 8 : 0, rock_pts[r_kills[i] < 5 ? r_kills[i] : 5]);
                    ++r_kills[i];
                    --enemies_left;
                    SFXP(ASSET__audio__squash_sfx_ID, 3);
                }
            }
            break;
        case RS_CRUMBLE:
            if (++r_timer[i] > 12) r_on[i] = 0;
            break;
        }
    }
}


static void kill_player(void)
{
    state = ST_DYING; state_timer = 0;
    ball_on = 0;
    stop_music();
    SFXP(ASSET__audio__die_sfx_ID, 5);
}


void play_update(void)
{
    unsigned char i, k;

    if (player1_new_buttons & INPUT_MASK_START) {
        state = ST_PAUSE;
        return;
    }
    player_update();
    rocks_update();
    if (c_on[0] | c_on[1] | c_on[2]) {
        k = vumpires_alive();
        for (i = 0; i < MAXC; ++i) {
            if (!c_on[i]) continue;
            if (!k) { c_on[i] = 0; continue; }                 /* nobody left to raise it */
            if (absdiff(px, c_x[i]) < 6 && absdiff(py, c_y[i]) < 6) {   /* Doug stomps the headstone */
                c_on[i] = 0; add_score(1); add_popup(c_x[i], c_y[i], 1);
                SFX(ASSET__audio__dig_sfx_ID);
            }
        }
    }
    enemies_update_all();

    /* deadly contact */
    if (enemies_touch_player()) { kill_player(); return; }
    for (i = 0; i < MAXR; ++i) {
        if (r_on[i] && r_state[i] == RS_FALL &&
            absdiff(px, r_c[i] << 3) < 7 && absdiff(py, r_y[i]) < 7) {
            kill_player();
            return;
        }
    }

    if (enemies_left == 0) {
        unsigned char alive = 0;
        for (i = 0; i < MAXE; ++i) if (e_state[i] != ES_NONE) alive = 1;
        if (!alive) {
            state = ST_CLEAR; state_timer = 0;
            stop_music();
            play_song(ASSET__audio__clear_mid, REPEAT_NONE);
        }
    }
}

/* start the round over after Doug is caught: Doug and the enemies go back to their start positions */
void reset_round(void)
{
    reset_player();
    reset_enemies_home();
    field_dirty = 1;
}
