/*
 * Drawing. The VIC-IV shows the field as a map of full-colour 8x8 characters: each game tile is three characters stacked
 * (pixel rows repeated 3x) and the hardware stretches them 4x wide, so a game pixel is 4x3 screen pixels.
 *
 * Everything that moves except Doug is drawn by the CPU into copies of the characters it covers ("software sprites"),
 * so there is no limit of eight. Each frame: start from the plain map, give every covered cell its own copy of its tile,
 * draw the sprites into the copies, upload the copies and the map into the buffer that is not being shown, then switch.
 * Doug is a full-colour hardware sprite.
 */
#include <stdint.h>
#include <string.h>
#include "platform.h"
#include "game.h"
#include "data.h"
#include "sprites.h"
#include "render.h"

#define CHAR_BASE    4096u                   /* first tile character: chip RAM $40000 */
#define N_FIELD      (FIELD_CH_COLS * FIELD_CH_ROWS)
#define TUNNEL_TILE0 N_FIELD                 /* tiles 208..223 are the tunnel tiles */
#define N_TILES      (N_FIELD + 16)
#define SY           4                       /* screen rows per game pixel (and character rows per tile row) */
#define SCREEN_ROWS  (FIELD_CH_ROWS * SY)
#define TITLE_CH0    5120u                   /* the title picture's characters: chip RAM $50000 (bank 5), 4 per tile like the field */
#define HUD_H        12                      /* the score strip above the field, in game pixels */
#define HUD_ROWS     (HUD_H * SY / 8)
#define HUD_CHARS    (FIELD_CH_COLS * HUD_ROWS)
#define HUD_CH0      (POOL_B + DYN_MAX)                    /* its characters (they never change places, only pixels) */
#define SCREEN_CHARS (FIELD_CH_COLS * SCREEN_ROWS)
#define TOTAL_CHARS  (HUD_CHARS + SCREEN_CHARS)
#define FIELD_OFS    (HUD_CHARS * 2)         /* byte offset of the field's rows inside a screen buffer */
#ifndef DYN_MAX
#define DYN_MAX      80                      /* characters that can be unique to one frame */
#endif
#define POOL_A       1280u                   /* their character numbers: two pools, one per screen buffer */
#define POOL_B       (POOL_A + DYN_MAX)
#define SCREEN_A     0x12000UL               /* two screen buffers, 2 bytes per character */
#define SCREEN_B     0x13000UL
#define COLOUR_OFS   0x2000u                 /* colour RAM offset, in $FF80000 */
#define XSCL         30                      /* character width = about 980/XSCL pixels: 30 gives 32, i.e. 4 per game pixel (measured in Xemu) */

/* Layout on the 640x400 display (window x 80..720, y 104..504), in physical pixels, all measured in Xemu.
 * The picture is 512x360 (strip and field), so to centre it the text area starts at 144,124. A sprite at register (X, Y) has its left edge at
 * screen x = 2X+31 and its top at screen y = Y. */
#define FIELD_X      144
#define TEXT_Y       4                                    /* top of the strip: the 512x360 picture is centred in 400 rows */
#define PIC_ROWS     ((HUD_ROWS + SCREEN_ROWS) * 8)       /* the whole picture, strip and field, in screen rows */
#define FIELD_Y      (TEXT_Y + HUD_ROWS * 8)

static uint16_t base_scr[SCREEN_ROWS][FIELD_CH_COLS];   /* the plain map: dirt and tunnels */
static uint16_t scr[SCREEN_ROWS][FIELD_CH_COLS];        /* this frame's map, with the copies in it */
static int8_t dyn_of[SCREEN_ROWS][FIELD_CH_COLS];       /* which copy a cell has this frame, or -1 */
static uint16_t dyn_cell[DYN_MAX];                      /* the cell of each copy: row * 16 + column */
static uint8_t dyn_n;
static uint8_t dyn_shadow[DYN_MAX][64];                 /* the copies, in normal memory while they are drawn into */
static uint8_t dyn_dummy[64];
static uint8_t charbuf[64], rowbuf[FIELD_CH_COLS * 2];
static uint8_t cur_buf;                                 /* the buffer being drawn (not the one being shown) */

/* ------------------------------------------------------------------ tiles -- */
/* The tile characters come from the TILES file on the disk, which start-up code copied into attic RAM (see early.s). */
static void build_tiles(void)
{
    dma_copy28(0x80, 0, 0, (uint32_t)CHAR_BASE << 6, (uint16_t)N_TILE_TOTAL * SY * 64);
}

/* which tile shows at map position (cc, rr)? cc is the character column (game cell column + 1) */
static uint8_t tunnel_mask(uint8_t c, uint8_t r)
{
    uint8_t m = 0;
    if (M(c, r - 1)) m |= 1;
    if (M(c + 1, r)) m |= 2;
    if (M(c, r + 1)) m |= 4;
    if (c == 0 || M(c - 1, r)) m |= 8;
    return m;
}

static uint16_t tile_at(uint8_t cc, uint8_t rr)
{
    if (rr >= 1 && cc >= 1 && cc <= COLS && M(cc - 1, rr) == 0)
        return TUNNEL_TILE0 + tunnel_mask(cc - 1, rr);          /* a dug cell: the tunnel tile for its neighbours */
    return (uint16_t)rr * FIELD_CH_COLS + cc;                   /* the dirt picture */
}

static uint8_t scene_title;                              /* 1 while the title screen is up */

static void rebuild_base(void)
{
    uint8_t rr, cc, k;
    uint16_t tile;
    if (scene_title) {
        for (rr = 0; rr < FIELD_CH_ROWS; ++rr)
            for (cc = 0; cc < FIELD_CH_COLS; ++cc)
                for (k = 0; k < SY; ++k) base_scr[rr * SY + k][cc] = TITLE_CH0 + ((uint16_t)rr * FIELD_CH_COLS + cc) * SY + k;
        return;
    }
    for (rr = 0; rr < FIELD_CH_ROWS; ++rr)
        for (cc = 0; cc < FIELD_CH_COLS; ++cc) {
            tile = tile_at(cc, rr);
            for (k = 0; k < SY; ++k) base_scr[rr * SY + k][cc] = CHAR_BASE + tile * SY + k;
        }
}

/* ------------------------------------------------------- software sprites -- */
static uint8_t *dyn_for(uint8_t cc, uint8_t cr)
{
    int8_t d = dyn_of[cr][cc];
    if (d < 0) {
        if (dyn_n >= DYN_MAX) return dyn_dummy;
        d = dyn_n++;
        dyn_of[cr][cc] = d;
        dyn_cell[d] = ((uint16_t)cr << 4) | cc;
        dma_char_in(base_scr[cr][cc], dyn_shadow[d]);               /* start from the plain character the map shows */
    }
    return dyn_shadow[d];
}

/* One game pixel is SY = 4 screen rows, and those four rows always sit in the same character (a game row y covers
 * character row y/2, at row offset (y%2)*4), so one lookup per 8 pixels gives the buffer to write four times. */
#define ROW_SETUP(cc, yy) pb = dyn_for((cc), (uint8_t)((yy) >> 1)) + (((yy) & 1) << 5)
#define PUT4(pb, cx, v) do { (pb)[(cx)] = (v); (pb)[(cx) + 8] = (v); (pb)[(cx) + 16] = (v); (pb)[(cx) + 24] = (v); } while (0)

static void draw_box(int16_t X, int16_t Y, uint8_t w, uint8_t h, uint8_t v)
{
    uint8_t i, j, cc, last, cx;
    uint8_t *pb;
    int16_t xx, yy;
    for (j = 0; j < h; ++j) {
        yy = Y + j;
        if (yy < 0 || yy >= 104) continue;
        last = 255;
        for (i = 0; i < w; ++i) {
            xx = X + i;
            if (xx < 0 || xx >= 128) continue;
            cc = (uint8_t)xx >> 3;
            if (cc != last) { last = cc; ROW_SETUP(cc, yy); }
            cx = (uint8_t)xx & 7;
            PUT4(pb, cx, v);
        }
    }
}

extern const uint8_t *br_src;                   /* blit.s */
extern uint8_t *br_dst;
extern uint8_t br_n;
uint8_t br_scan(void);
void blit_run(void);

/* rows r0..r0+rows-1 of a sprite, with its top-left at plane position (X, Y). Each row is cut into runs that lie in one
 * character; the character copy is only looked up (or made) once a run has a visible pixel. */
static void draw_img(const uint8_t *px, uint8_t w, uint8_t h, int16_t X, int16_t Y, uint8_t r0, uint8_t rows)
{
    uint8_t sy, sx, sxe, take, k, xx, x0 = (uint8_t)X, cr, yr;
    const uint8_t *src;
    int16_t yy = Y + r0;
    if ((uint8_t)(r0 + rows) < h) h = r0 + rows;
    if (X >= 128 || X + w <= 0) return;
    sx = (X < 0) ? (uint8_t)(-X) : 0;                  /* clip to the picture */
    sxe = (X + w > 128) ? (uint8_t)(128 - X) : w;
    src = px + (uint16_t)r0 * w;
    for (sy = r0; sy < h; ++sy, ++yy, src += w) {
        if ((uint16_t)yy >= 104) continue;
        cr = (uint8_t)yy >> 1;
        yr = ((uint8_t)yy & 1) << 5;
        for (k = sx; k < sxe; k += take) {
            xx = x0 + k;
            take = 8 - (xx & 7);
            if (take > sxe - k) take = sxe - k;
            br_src = src + k; br_n = take;
            if (br_scan()) {
                br_dst = dyn_for(xx >> 3, cr) + yr + (xx & 7);
                blit_run();
            }
        }
    }
}

static void draw_soft(uint8_t idx, int16_t X, int16_t Y, uint8_t r0, uint8_t rows)
{
    const SoftSprite *s = &soft_sprites[idx];
    draw_img(s->px, s->w, s->h, X, Y, r0, rows);
}

#define FRAME_T   16832                      /* timer ticks in a 60 Hz frame (measured in Xemu) */
#define TICK_MIN  (FRAME_T + FRAME_T / 2)    /* a game tick is two frames (30 a second, as on the GameTank): flip on the first frame boundary after 1.5 */
static uint16_t last_flip;

static void commit_and_show(void)
{
    uint8_t d;
    uint16_t cell, pool = cur_buf ? POOL_B : POOL_A;
    dma_copy(base_scr, SCREEN_CHARS * 2, 0, (uint32_t)(uint16_t)scr);
    for (d = 0; d < dyn_n; ++d) {
        cell = dyn_cell[d];
        dma_char_out(dyn_shadow[d], pool + d);
        scr[cell >> 4][cell & 15] = pool + d;
    }
    dma_copy(scr, SCREEN_CHARS * 2, 0, (cur_buf ? SCREEN_B : SCREEN_A) + FIELD_OFS);
    wait_frame();
    while ((uint16_t)(last_flip - timer_now()) < TICK_MIN) wait_frame();    /* normally every second frame, later if the drawing took longer */
    POKE(0xD061, cur_buf ? (uint8_t)(SCREEN_B >> 8) : (uint8_t)(SCREEN_A >> 8));        /* show it */
    POKE(0xD062, cur_buf ? (uint8_t)(SCREEN_B >> 16) : (uint8_t)(SCREEN_A >> 16));
    last_flip = timer_now();
    cur_buf ^= 1;
}

/* ----------------------------------------------------------- the game -- */
static const uint8_t S_GRUB[4]   = { SPR_GRUB_0_0, SPR_GRUB_0_1, SPR_GRUB_1_0, SPR_GRUB_1_1 };
static const uint8_t S_EMB[4]    = { SPR_EMB_0_0, SPR_EMB_0_1, SPR_EMB_1_0, SPR_EMB_1_1 };
static const uint8_t S_GK[4]     = { SPR_GK_0_0, SPR_GK_0_1, SPR_GK_1_0, SPR_GK_1_1 };
static const uint8_t S_MASCOT[4] = { SPR_MASCOT_0_0, SPR_MASCOT_0_1, SPR_MASCOT_1_0, SPR_MASCOT_1_1 };
static const uint8_t S_BAT[2]    = { SPR_BAT0, SPR_BAT1 };
static const uint8_t S_GHOST[2]  = { SPR_GHOST0, SPR_GHOST1 };
static const uint8_t S_FLAME[2]  = { SPR_FLAME0, SPR_FLAME1 };
static const uint8_t S_BURST[4]  = { SPR_BURST0, SPR_BURST1, SPR_BURST2, SPR_BURST3 };
static const uint8_t S_MARK[8]   = { SPR_MARK_X17, SPR_MARK_X27, SPR_MARK_X263, SPR_MARK_X363, SPR_MARK_X463, SPR_MARK_X391, SPR_MARK_X591, SPR_MARK_X691 };
static const signed char DX[4] = { 1, -1, 0, 0 };
static const signed char DY[4] = { 0, 0, -1, 1 };
static const signed char ORB_X[8] = { 0, 6, 9, 6, 0, -6, -9, -6 };
static const signed char ORB_Y[8] = { -9, -6, 0, 6, 9, 6, 0, -6 };
#define ORB_K(n) ((uint8_t)(((frame_ct >> 2) + ((n) << 2)) & 7))
#define WINDUP 10
#define COL_WHITE 7          /* GameTank palette indices the drawing uses for its few plain boxes */
#define COL_FLAME3 91
#define COL_HOSE_D 44

static void draw_rocks(void)
{
    uint8_t i, f;
    int16_t x, y;
    for (i = 0; i < MAXR; ++i) {
        if (!r_on[i]) continue;
        x = 8 + (r_c[i] << 3);
        y = (r_state[i] == RS_FALL || r_state[i] == RS_CRUMBLE) ? r_y[i] : (r_r[i] << 3);
        switch (r_state[i]) {
        case RS_WOBBLE: f = (r_timer[i] >> 1) & 1; draw_soft(f ? SPR_ROCKW1 : SPR_ROCKW0, x, y, 0, 8); break;
        case RS_CRUMBLE: f = r_timer[i] > 5; draw_soft(f ? SPR_ROCKC1 : SPR_ROCKC0, x, y, 0, 8); break;
        default: draw_soft(SPR_ROCK, x, y, 0, 8);
        }
    }
}

static void draw_mark(uint8_t i, int16_t x, int16_t y)
{
    uint8_t sn = e_infl[i], m;
    if (e_type[i] != 4) m = (sn == 1) ? 0 : (sn == 2) ? 2 : 5;
    else m = (sn == 1) ? 0 : (sn == 2) ? 1 : (sn == 3) ? 3 : (sn == 4) ? 4 : 6;
    draw_soft(S_MARK[m], x, y - ((e_type[i] == 4) ? 12 : 8), 0, 6);
}

static void draw_enemy(uint8_t i)
{
    uint8_t st = e_state[i], f, sz, k;
    int16_t x = 8 + e_x[i], y = e_y[i];
    f = ((frame_ct >> 2) & 1);
    k = e_face[i] * 2 + f;
    if (e_type[i] == 1 && (st == ES_WALK || st == ES_FLAME)) {          /* the two fireballs circling a Heater */
        for (sz = 0; sz < 2; ++sz)
            draw_soft(S_FLAME[(f + sz) & 1], x + ORB_X[ORB_K(sz)], y + ORB_Y[ORB_K(sz)], 0, 8);
    }
    switch (st) {
    case ES_WALK:
        if (e_type[i] == 4) { draw_soft(S_MASCOT[k], x - 4, y - 4, 0, 16); if (e_infl[i]) draw_mark(i, x, y); }
        else if (e_type[i] == 3) draw_soft(S_GK[k], x, y, 0, 8);
        else if (e_type[i])      draw_soft(S_EMB[k], x, y, 0, 8);
        else                     draw_soft(S_GRUB[k], x, y, 0, 8);
        break;
    case ES_FLAME:
        if (e_type[i] == 0) {                                            /* ritual: the Vumpire glows red */
            draw_soft(S_GRUB[k], x, y, 0, 8);
            if (e_timer[i] & 4) {
                draw_box(x - 1, y - 1, 2, 2, COL_FLAME3); draw_box(x + 7, y - 1, 2, 2, COL_FLAME3);
                draw_box(x - 1, y + 7, 2, 2, COL_FLAME3); draw_box(x + 7, y + 7, 2, 2, COL_FLAME3);
            }
            break;
        }
        if (e_timer[i] < WINDUP && ((e_timer[i] >> 1) & 1)) draw_soft(S_EMB[e_face[i] * 2], x, y, 0, 8);
        else                                                draw_soft(S_EMB[e_face[i] * 2 + 1], x, y, 0, 8);
        if (e_timer[i] >= WINDUP) {
            uint8_t n, fr = (frame_ct >> 1) & 1;
            for (n = 0; n < e_flen[i]; ++n)
                draw_soft(S_FLAME[fr ^ (n & 1)], (e_face[i] == DIR_R) ? x + 8 + (n << 3) : x - 8 - (n << 3), y, 0, 8);
        }
        break;
    case ES_GHOST:
        f = (frame_ct >> 3) & 1;
        if (e_type[i] == 2) draw_soft(S_BAT[(frame_ct >> 1) & 1], x - 2, y, 0, 8);
        else                draw_soft(S_GHOST[f], x, y, 0, 8);
        break;
    case ES_INFL:                                                        /* stunned: normal body plus a mark per strike */
        if (e_type[i] == 4)      draw_soft(S_MASCOT[k], x - 4, y - 4, 0, 16);
        else if (e_type[i] == 3) draw_soft(S_GK[k], x, y, 0, 8);
        else if (e_type[i] == 2) draw_soft(S_BAT[0], x - 2, y, 0, 8);
        else if (e_type[i])      draw_soft(S_EMB[k], x, y, 0, 8);
        else                     draw_soft(S_GRUB[k], x, y, 0, 8);
        draw_mark(i, x, y);
        break;
    case ES_POP:                                                         /* out! a little firework */
        k = e_timer[i] >> 1;
        if (k > 3) k = 3;
        sz = soft_sprites[S_BURST[k]].w;
        draw_soft(S_BURST[k], x + 4 - (sz >> 1), y + 4 - (sz >> 1), 0, sz);
        if (e_timer[i] < 6) draw_soft(S_MARK[(e_type[i] == 4) ? 7 : 5], x, y - ((e_type[i] == 4) ? 12 : 8), 0, 6);
        break;
    case ES_SQUASH:
        if (e_type[i] == 3)       draw_soft(S_GK[k], x, y, 4, 4);
        else if (e_type[i] == 2)  draw_soft(S_BAT[0], x - 2, y, 4, 4);
        else if (e_type[i])       draw_soft(S_EMB[k], x, y, 4, 4);
        else                      draw_soft(S_GRUB[k], x, y, 4, 4);
        break;
    }
}

static void draw_ball(void)
{
    int16_t bx, by;
    if (!ball_on) return;
    bx = 8 + ball_x - 2; by = ball_y - 2;
    draw_box(bx - DX[ball_dir] * 3, by - DY[ball_dir] * 3, 4, 4, COL_HOSE_D);     /* motion streak */
    draw_box(bx, by, 4, 4, COL_WHITE);
    draw_box(bx + 1, by + 1, 2, 1, COL_FLAME3);                                     /* seam */
}

/* text on the picture, with the game's font (set 0 = cream, 1 = gold) */
static void draw_text(int16_t x, int16_t y, const char *str, uint8_t set)
{
    uint8_t ch, idx;
    while ((ch = (uint8_t)*str++) != 0) {
        if (ch >= 'A' && ch <= 'Z') idx = ch - 'A';
        else if (ch >= '0' && ch <= '9') idx = 26 + ch - '0';
        else if (ch == '-') idx = 36;
        else if (ch == ':') idx = 37;
        else if (ch == '!') idx = 38;
        else if (ch == '.') idx = 39;
        else if (ch == '\'') idx = 40;
        else { x += 4; continue; }
        draw_img(font_px[set * 41 + idx], 4, 6, x, y, 0, 6);
        x += 4;
    }
}

/* ----------------------------------------------------------------- Doug -- */
static void draw_doug(void)
{
    uint8_t f = (frame_ct >> 2) & 1;
    uint8_t idx = (state == ST_DYING) ? (uint8_t)(SPR_DOUG_X0 + f) : (uint8_t)(SPR_DOUG_0_0 + pdir * 2 + (pmoving ? ((panim >> 2) & 1) : 0));
    draw_soft(idx, 8 + px, py, 0, 8);
}

/* ------------------------------------------------------------------- HUD -- */
/* The strip is drawn into a 128x16 picture (one byte per game pixel, in the sprite copies' spare memory), then cut into its 96 characters. It only
 * changes when the score, lives or inning do. */
#define hud_pic ((uint8_t (*)[128])dyn_shadow)          /* the sprite copies are free at this point of a frame */
static uint8_t hud_lives, hud_level, hud_drawn;
static uint16_t hud_score_h; static uint8_t hud_score_t;

#define HUD_TY       3                       /* the text and the life icons are game rows 3-8 of the strip */
#define HUD_TEXT_R0  ((HUD_TY * SY) / 8)
#define HUD_TEXT_R1  (((HUD_TY + 6) * SY + 7) / 8)
static const uint8_t HUD_SRC_ROW[48] = { 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 9, 10, 10, 10, 10, 11, 11, 11, 11 };

static void hud_text(uint8_t x, uint8_t y, const char *str, uint8_t set)
{
    uint8_t ch, idx, i, j;
    const uint8_t *g;
    while ((ch = (uint8_t)*str++) != 0) {
        if (ch >= 'A' && ch <= 'Z') idx = ch - 'A';
        else if (ch >= '0' && ch <= '9') idx = 26 + ch - '0';
        else if (ch == '-') idx = 36;
        else if (ch == ':') idx = 37;
        else if (ch == '!') idx = 38;
        else if (ch == '.') idx = 39;
        else if (ch == '\'') idx = 40;
        else { x += 4; continue; }
        g = font_px[set * 41 + idx];
        for (j = 0; j < 6; ++j)
            for (i = 0; i < 4; ++i)
                if (g[j * 4 + i]) hud_pic[y + j][x + i] = g[j * 4 + i];
        x += 4;
    }
}

static void hud_build(void)
{
    uint8_t i, j, n, cx, cy, py;
    uint16_t v;
    char buf[8];
    memset(hud_pic, COL_INK, HUD_H * 128);
    memset(hud_pic[HUD_H - 1], COL_RIM, 128);
    v = scene_title ? hi_h : score_h;
    for (i = 5; i > 0; --i) { buf[i - 1] = '0' + (v % 10); v /= 10; }
    buf[5] = '0' + (scene_title ? hi_t : score_t); buf[6] = '0'; buf[7] = 0;
    hud_text(10, HUD_TY, scene_title ? "BEST" : "RUNS", 1);
    hud_text(30, HUD_TY, buf, 0);
    if (!scene_title) {
        hud_text(62, HUD_TY, "INN", 1);
        buf[0] = '0' + (level / 10) % 10; buf[1] = '0' + level % 10; buf[2] = 0;
        hud_text(74, HUD_TY, buf, 0);
    }
    for (n = 0; n < (scene_title ? 0 : lives) && n < 5; ++n)
        for (j = 0; j < 6; ++j)
            for (i = 0; i < 8; ++i)
                if (life_px[j * 8 + i]) hud_pic[HUD_TY + j][121 - (n << 3) - 8 + i] = life_px[j * 8 + i];
    /* each character row is 8 screen rows = 8/3 game rows */
    for (cy = hud_drawn ? HUD_TEXT_R0 : 0; cy < (hud_drawn ? HUD_TEXT_R1 : HUD_ROWS); ++cy)   /* only the rows with text after the first time */
        for (cx = 0; cx < FIELD_CH_COLS; ++cx) {
            for (py = 0; py < 8; ++py)
                memcpy(charbuf + py * 8, hud_pic[HUD_SRC_ROW[cy * 8 + py]] + cx * 8, 8);
            dma_copy(charbuf, 64, 0, ((uint32_t)(HUD_CH0 + cy * FIELD_CH_COLS + cx)) << 6);
        }
    hud_score_h = scene_title ? hi_h : score_h; hud_score_t = scene_title ? hi_t : score_t; hud_lives = lives; hud_level = level; hud_drawn = 1;
}

static void hud_update(void)
{
    if (!hud_drawn || (scene_title ? hi_h : score_h) != hud_score_h || (scene_title ? hi_t : score_t) != hud_score_t || lives != hud_lives || level != hud_level)
        hud_build();
}

/* the title screen: Doug runs along the long tunnel with a Vumpire and a Heater after him. Positions repeat every 128 half-ticks,
 * which matches the 8-bit tick counter, so the chase loops without a jump. */
static void draw_title(void)
{
    uint8_t t = frame_ct >> 1, f = (frame_ct >> 2) & 1, p;
    p = t & 127;
    if (p < 112) draw_soft(SPR_DOUG_0_0 + f, 8 + p, 72, 0, 8);
    p = (t - 24) & 127;
    if (p < 112) draw_soft(S_GRUB[f], 8 + p, 72, 0, 8);
    p = (t - 44) & 127;
    if (p < 112) draw_soft(S_EMB[f], 8 + p, 72, 0, 8);
    if ((frame_ct & 32) == 0) draw_text(44, 40, "PUSH START", 1);
}

void render_frame(void)
{
    uint8_t i, want_title = (state == ST_TITLE);
    if (want_title != scene_title) { scene_title = want_title; field_dirty = 1; hud_drawn = 0; }
    if (field_dirty) { rebuild_base(); field_dirty = 0; }
    hud_update();
    dyn_n = 0;
    memset(dyn_of, 0xFF, sizeof dyn_of);
    if (scene_title) draw_title();
    else {
        draw_rocks();
        for (i = 0; i < MAXC; ++i)
            if (c_on[i]) draw_soft(SPR_TOMB, 8 + c_x[i], c_y[i], 0, 8);
        for (i = 0; i < MAXE; ++i)
            if (e_state[i] != ES_NONE) draw_enemy(i);
        draw_ball();
        draw_doug();
    }
    commit_and_show();
}

/* ------------------------------------------------------------------- setup -- */
static void set_palette(uint8_t bank_sel, const uint8_t (*rgb)[3], uint16_t n)
{
    uint16_t i;
    POKE(0xD070, bank_sel);
    for (i = 0; i < n; ++i) {
        uint8_t r = rgb[i][0], g = rgb[i][1], b = rgb[i][2];
        POKE(0xD100 + i, (r << 4) | (r >> 4));      /* palette values are stored with their nybbles swapped */
        POKE(0xD200 + i, (g << 4) | (g >> 4));
        POKE(0xD300 + i, (b << 4) | (b >> 4));
    }
}

void render_init(void)
{
    uint8_t i;
    POKE(0xD02F, 0x47); POKE(0xD02F, 0x53);          /* unlock the VIC-IV registers */
    POKE(0xD06F, PEEK(0xD06F) | 0x80);               /* 60 Hz (NTSC) timing, so two frames are exactly one 30 Hz game tick */
    wait_frame(); wait_frame(); wait_frame();        /* let the mode change settle before the registers that it resets are set */
    POKE(0xD030, PEEK(0xD030) | 0x04);               /* colours 0-15 come from the palette RAM too */
    /* palette bank 1 (mapped in) is for the characters */
    set_palette(0x40 | 0x10 | 0x08, palette_rgb, 256);
    POKE(0xD070, 0x10 | 0x08);                       /* characters use bank 1, sprites bank 2 */
    POKE(0xD020, BLANK_PIXEL); POKE(0xD021, BLANK_PIXEL);
    POKE(0xD031, PEEK(0xD031) | 0x88);               /* H640 + V400: 640x400. (Writing $D031 resets the registers below.) */
    POKE(0xD054, 0x05);                              /* CHR16 (13-bit character numbers) + full-colour for chars > $FF */
    POKE(0xD05A, XSCL);                              /* characters 4 screen pixels per game pixel across */
    POKE(0xD05E, FIELD_CH_COLS);                     /* characters per row */
    POKE(0xD058, FIELD_CH_COLS * 2); POKE(0xD059, 0);/* bytes per row */
    POKE(0xD07B, HUD_ROWS + SCREEN_ROWS - 1);                   /* rows, minus one */
    POKE(0xD060, (uint8_t)SCREEN_A);
    POKE(0xD061, (uint8_t)(SCREEN_A >> 8));
    POKE(0xD062, (uint8_t)(SCREEN_A >> 16));
    POKE(0xD063, PEEK(0xD063) & 0xF0);
    POKE(0xD064, (uint8_t)COLOUR_OFS);
    POKE(0xD065, (uint8_t)(COLOUR_OFS >> 8));
    POKE(0xD04C, (uint8_t)FIELD_X);                  /* text area start: centre the picture in the window */
    POKE(0xD04D, (PEEK(0xD04D) & 0xF0) | (FIELD_X >> 8));
    POKE(0xD04E, (uint8_t)TEXT_Y);
    POKE(0xD04F, (PEEK(0xD04F) & 0xF0) | (TEXT_Y >> 8));
    POKE(0xD048, (uint8_t)TEXT_Y);                   /* borders just outside the picture (the default ones cut it off at 400 rows) */
    POKE(0xD049, (PEEK(0xD049) & 0xF0) | (TEXT_Y >> 8));
    POKE(0xD04A, (uint8_t)(TEXT_Y + PIC_ROWS));
    POKE(0xD04B, (PEEK(0xD04B) & 0xF0) | ((TEXT_Y + PIC_ROWS) >> 8));
    POKE(0xD05C, 0x10);                              /* narrow side borders */
    POKE(0xD05D, PEEK(0xD05D) & 0xC0);

    build_tiles();
    dma_copy28(0x80, 0x10000UL, 0, (uint32_t)TITLE_CH0 << 6, 53248);      /* the title picture: attic file 1 -> characters */
    dma_fill(0, TOTAL_CHARS * 2, 0xFF, 0x80000UL + COLOUR_OFS);       /* colour RAM: plain characters */

    POKE(0xD015, 0x00);                              /* no hardware sprites: Doug is drawn like the others */

    for (i = 0; i < HUD_ROWS; ++i) {                 /* the strip's character numbers, one row at a time, into both buffers */
        uint8_t k;
        for (k = 0; k < FIELD_CH_COLS; ++k) ((uint16_t *)rowbuf)[k] = HUD_CH0 + i * FIELD_CH_COLS + k;
        dma_copy(rowbuf, FIELD_CH_COLS * 2, 0, SCREEN_A + (uint16_t)i * FIELD_CH_COLS * 2);
        dma_copy(rowbuf, FIELD_CH_COLS * 2, 0, SCREEN_B + (uint16_t)i * FIELD_CH_COLS * 2);
    }
    field_dirty = 1;
    rebuild_base();
    field_dirty = 0;
    cur_buf = 0;
}
