#ifndef GAME_H
#define GAME_H
/* shared constants and state of the game logic (see game.c) */
#define COLS 14
#define ROWS 13
#define MAXE 6
#define MAXR 5
#define MAXC 3
#define MAXP 4
#define INNINGS 9

enum { ST_TITLE, ST_READY, ST_PLAY, ST_DYING, ST_CLEAR, ST_OVER, ST_PAUSE, ST_INTRO, ST_WIN, ST_ATTRACT };
enum { ES_NONE, ES_WALK, ES_GHOST, ES_INFL, ES_POP, ES_SQUASH, ES_FLAME };
enum { RS_STILL, RS_WOBBLE, RS_FALL, RS_CRUMBLE };
enum { DIR_R, DIR_L, DIR_U, DIR_D };

#define INPUT_MASK_UP    1
#define INPUT_MASK_DOWN  2
#define INPUT_MASK_LEFT  4
#define INPUT_MASK_RIGHT 8
#define INPUT_MASK_A     16
#define INPUT_MASK_START 32

/* 0 = tunnel, 1 = dirt, 2 = boulder cell; 16-wide rows so index = r<<4|c. Columns 14/15 and row 13 are solid padding. */
extern unsigned char map[(ROWS + 1) * 16];
extern unsigned char paid[(ROWS + 1) * 16];
#define M(c, r) map[(((unsigned char)(r)) << 4) | ((unsigned char)(c))]

extern unsigned char state, state_timer, frame_ct, level, lives;
extern unsigned char bob;                  /* 1 while the player and the walkers nod their heads (on the theme's snare hits) */
void beat_start(void);                     /* the theme has just started */
void beat_tick(void);                      /* once per game tick */
extern unsigned int score_h, hi_h, next_life_h;
extern unsigned char score_t, hi_t, score_dirty;
extern unsigned char new_best;      /* the run that just ended set a new best score (defined in main.c) */
extern unsigned int lfsr, run_seed;
extern unsigned char px, py, pdir, panim, pmoving;
extern unsigned char ball_on, ball_x, ball_y, ball_dir, ball_dist, throw_cd, ball_dirt;
extern unsigned char e_state[MAXE], e_type[MAXE], e_x[MAXE], e_y[MAXE], e_dir[MAXE], e_face[MAXE];
extern unsigned char e_infl[MAXE], e_timer[MAXE], e_acc[MAXE], e_homec[MAXE], e_homer[MAXE], e_flen[MAXE];
extern unsigned char e_flee[MAXE], e_pts[MAXE];
extern unsigned char e_prevc[MAXE], e_prevr[MAXE];
extern unsigned char c_on[MAXC], c_slot[MAXC], c_x[MAXC], c_y[MAXC];
extern unsigned char enemies_left, espeed;
extern unsigned char r_on[MAXR], r_c[MAXR], r_r[MAXR], r_y[MAXR], r_state[MAXR], r_timer[MAXR], r_kills[MAXR];
extern unsigned char pop_t[MAXP], pop_x[MAXP], pop_y[MAXP];
extern unsigned int pop_v[MAXP];
extern unsigned char gold_on, gold_c, gold_r;   /* a gold bar lying in one enemy cave: 500 points */
extern unsigned char field_dirty;
extern int player1_buttons, player1_new_buttons;

void add_score(unsigned int h);   /* hundreds of points: also the best score and extra lives */
void build_level(void);            /* a new inning's map, pockets, enemies and boulders (seeded by run_seed and level) */
void rocks_update(void);          /* boulders keep falling while the screen shows Doug being caught */
void play_update(void);            /* one game tick */
void reset_round(void);            /* Doug and the enemies back to their start positions */
#endif
