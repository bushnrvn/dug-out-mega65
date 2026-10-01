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
#define BANNER_CH0   1920u                   /* the banner's characters: chip RAM $1E000 */
#define BAN_TX       10                      /* the banner is 10 x 3 tiles: 80 x 24 game pixels */
#define BAN_TY       3
#define BAN_COL      3                       /* its place on the field, in tiles */
#define BAN_ROW      5
#define HUD_H        12                      /* the score strip above the field, in game pixels */
#define HUD_ROWS     (HUD_H * SY / 8)
#define HUD_CHARS    (FIELD_CH_COLS * HUD_ROWS)
#define HUD_CH0      1220u                   /* chip RAM $13100 */                    /* its characters (they never change places, only pixels) */
#define SCREEN_CHARS (FIELD_CH_COLS * SCREEN_ROWS)
#define TOTAL_CHARS  (HUD_CHARS + SCREEN_CHARS)
#define FIELD_OFS    (HUD_CHARS * 2)         /* byte offset of the field's rows inside a screen buffer */
#ifndef DYN_MAX
#define DYN_MAX      152                     /* characters that can be unique to one frame (the most seen: 134, on the victory screen) */
#endif
#define POOL_A       1316u                   /* their character numbers: two pools, one per screen buffer */
#define POOL_B       (POOL_A + DYN_MAX)
#define SCREEN_A     0x12100UL               /* two screen buffers, 2 bytes per character */
#define SCREEN_B     0x12900UL
#define COLOUR_OFS   0x2000u                 /* colour RAM offset, in $FF80000 */
#ifndef XSCL
#define XSCL         26                      /* character width = about 980/XSCL pixels (measured in Xemu: 30 gives 4 per game pixel, 26 gives 4.6).
                                                The MEGA65's 720x480 picture is shown as 4:3, so its pixels are about 11% narrower than square; 4.6 (15% wider)
                                                makes the picture square on a 4:3 display and on a 5:4 monitor, which squeezes it a little more. */
#endif

/* Layout on the 640x400 display (window x 80..720, y 104..504), in physical pixels, all measured in Xemu.
 * The picture is 512x360 (strip and field), so to centre it the text area starts at 144,124 (with the wider characters, 104,124). A sprite at register (X, Y) has its left edge at
 * screen x = 2X+31 and its top at screen y = Y. */
#define FIELD_X      104                                  /* (144 for 4 pixels per game pixel; less as the picture widens, to keep it centred) */
#define TEXT_Y       4                                    /* top of the strip: the 512x360 picture is centred in 400 rows */
#define PIC_ROWS     ((HUD_ROWS + SCREEN_ROWS) * 8)       /* the whole picture, strip and field, in screen rows */
#define FIELD_Y      (TEXT_Y + HUD_ROWS * 8)

static uint16_t base_scr[SCREEN_ROWS][FIELD_CH_COLS];   /* the plain map: dirt and tunnels */
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

enum { SC_GAME, SC_TITLE, SC_OVER, SC_WIN, SC_INTRO };
static uint8_t scene;                                    /* which screen is up */
#define scene_title (scene == SC_TITLE)
#define ART_STRIP_CH0 TITLE_CH0                          /* the over and win pictures: 96 strip characters, then the field's */

static uint8_t banner_on;

static void rebuild_base_plain(void)
{
    uint8_t rr, cc, k;
    uint16_t tile;
    if (scene != SC_GAME) {                              /* a picture from the disk (the title shows only the field part) */
        uint16_t c0 = TITLE_CH0 + ((scene == SC_TITLE || scene == SC_INTRO) ? 0 : HUD_CHARS);
        for (rr = 0; rr < FIELD_CH_ROWS; ++rr)
            for (cc = 0; cc < FIELD_CH_COLS; ++cc)
                for (k = 0; k < SY; ++k) base_scr[rr * SY + k][cc] = c0 + ((uint16_t)rr * FIELD_CH_COLS + cc) * SY + k;
        return;
    }
    for (rr = 0; rr < FIELD_CH_ROWS; ++rr)
        for (cc = 0; cc < FIELD_CH_COLS; ++cc) {
            tile = tile_at(cc, rr);
            for (k = 0; k < SY; ++k) base_scr[rr * SY + k][cc] = CHAR_BASE + tile * SY + k;
        }
}

static void rebuild_base(void)
{
    uint8_t tx, ty, k;
    rebuild_base_plain();
    if (banner_on && scene == SC_GAME)                 /* the banner's tiles over the field */
        for (ty = 0; ty < BAN_TY; ++ty)
            for (tx = 0; tx < BAN_TX; ++tx)
                for (k = 0; k < SY; ++k)
                    base_scr[(BAN_ROW + ty) * SY + k][BAN_COL + tx] = BANNER_CH0 + (ty * BAN_TX + tx) * SY + k;
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

extern uint16_t br_src;                          /* blit.s: where in chip RAM the run's pixels are: a bank and an offset in it */
extern uint8_t br_bank;
extern uint8_t *br_dst;
extern uint8_t br_n;
uint8_t br_scan(void);
void blit_run(void);

/* rows r0..r0+rows-1 of a sprite, with its top-left at plane position (X, Y). Each row is cut into runs that lie in one
 * character; the character copy is only looked up (or made) once a run has a visible pixel. */
/* Where the sprites' pixels and the font's glyph images are in chip RAM. (Not in bank 1: its first 8K belongs to the C65 system,
 * which keeps its disk state there, and the KERNAL stops finding the drive if that is overwritten.) Both are in the unused tails
 * of the banks that hold the tiles (bank 4) and the pictures (bank 5). */
#define SPR_BANK     4
#define SPR_BASE     0xE000u                     /* $4E000 */
#define GLYPH_BANK   5
#define GLYPH_BASE   0xE800u                     /* $5E800, just after the pictures' characters (928 of them from $50000) */

static void draw_img(uint16_t px, uint8_t bank, uint8_t w, uint8_t h, int16_t X, int16_t Y, uint8_t r0, uint8_t rows)
{
    uint8_t sy, sx, sxe, take, k, xx, x0 = (uint8_t)X, cr, yr;
    uint16_t src;
    int16_t yy = Y + r0;
    if ((uint8_t)(r0 + rows) < h) h = r0 + rows;
    if (X >= 128 || X + w <= 0) return;
    br_bank = bank;
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
            if (banner_on && (uint8_t)((xx >> 3) - BAN_COL) < BAN_TX && (uint8_t)((yy >> 3) - BAN_ROW) < BAN_TY)
                continue;                                /* nothing is drawn over a banner (it is eight game pixels per tile) */
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
    draw_img(SPR_BASE + s->off, SPR_BANK, s->w, s->h, X, Y, r0, rows);
}

#define FRAME_T   16832                      /* timer ticks in a 60 Hz frame (measured in Xemu) */
#define TICK_MIN  (FRAME_T + FRAME_T / 2)    /* a game tick is two frames (30 a second, as on the GameTank): flip on the first frame boundary after 1.5 */
static uint16_t last_flip;

extern uint32_t pk_base;                         /* blit.s: store one character number into the screen map in chip RAM */
extern uint16_t pk_off, pk_val;
void poke_cell(void);

static void commit_and_show(void)
{
    uint8_t d;
    uint16_t pool = cur_buf ? POOL_B : POOL_A;
    pk_base = (cur_buf ? SCREEN_B : SCREEN_A) + FIELD_OFS;
    dma_copy(base_scr, SCREEN_CHARS * 2, 0, pk_base);       /* the plain map ... */
    for (d = 0; d < dyn_n; ++d) {                           /* ... with each cell that has a copy pointing at it */
        dma_char_out(dyn_shadow[d], pool + d);
        pk_off = dyn_cell[d] << 1; pk_val = pool + d;
        poke_cell();
    }
    wait_frame();
    for (d = 0; d < 4 && (uint16_t)(last_flip - timer_now()) < TICK_MIN; ++d)    /* normally every second frame, later if the drawing took longer; */
        wait_frame();                                                              /* at most a few, so a stopped timer cannot hang the game */
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
#define COL_HAT 31
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
    if (e_type[i] == 1 && (st == ES_WALK || st == ES_FLAME)) {          /* the fireball circling a Heater */
        draw_soft(S_FLAME[f & 1], x + ORB_X[ORB_K(0)], y + ORB_Y[ORB_K(0)], 0, 8);
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

/* which of the font's 41 glyphs a character is, or 255 for a space or anything else */
static uint8_t glyph_index(uint8_t ch)
{
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= '0' && ch <= '9') return 26 + ch - '0';
    if (ch == '-') return 36;
    if (ch == ':') return 37;
    if (ch == '!') return 38;
    if (ch == '.') return 39;
    if (ch == '\'') return 40;
    return 255;
}

/* text on the picture, with the game's font (set 0 = cream, 1 = gold); only the lit pixels are drawn */
static void draw_text(int16_t x, int16_t y, const char *str, uint8_t set)
{
    uint8_t ch, idx;
    while ((ch = (uint8_t)*str++) != 0) {
        idx = glyph_index(ch);
        if (idx != 255) draw_img(GLYPH_BASE + (uint16_t)(set * 41 + idx) * 24, GLYPH_BANK, 4, 6, x, y, 0, 6);
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
    uint8_t ch, idx, i, j, col = set ? FONT_FG1 : FONT_FG0;
    while ((ch = (uint8_t)*str++) != 0) {
        idx = glyph_index(ch);
        if (idx == 255) { x += 4; continue; }
        for (j = 0; j < 6; ++j)
            for (i = 0; i < 4; ++i)
                if (font_bits[idx][(j * 4 + i) >> 3] & (1 << ((j * 4 + i) & 7))) hud_pic[y + j][x + i] = col;
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
    if (scene != SC_INTRO) {                          /* the intro and attract screens leave the strip empty */
        hud_text(10, HUD_TY, scene_title ? "BEST" : "RUNS", 1);
        hud_text(30, HUD_TY, buf, 0);
    }
    if (scene == SC_GAME) {
        hud_text(62, HUD_TY, "INN", 1);
        buf[0] = '0' + (level / 10) % 10; buf[1] = '0' + level % 10; buf[2] = 0;
        hud_text(74, HUD_TY, buf, 0);
    }
    for (n = 0; n < (scene == SC_GAME ? lives : 0) && n < 5; ++n)
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

static void fmt7(char *buf, uint16_t v, uint8_t t)       /* a score: five digits of hundreds, the tens digit, and a zero */
{
    uint8_t i;
    for (i = 5; i > 0; --i) { buf[i - 1] = '0' + (v % 10); v /= 10; }
    buf[5] = '0' + t; buf[6] = '0'; buf[7] = 0;
}

static void draw_text_center(int16_t y, const char *str, uint8_t set)
{
    uint8_t n = 0;
    while (str[n]) ++n;
    draw_text(64 - (n << 1), y, str, set);
}

/* The over and win pictures have a plaque along the bottom for the score; the numbers are drawn live. For the first
 * moments the score shows, then it alternates with PRESS START. (Text y is the picture's row minus the 12 rows of the strip.) */
static void draw_score_plaque(void)
{
    char buf[8];
    if ((frame_ct & 64) == 0 || state_timer < 40) {
        fmt7(buf, score_h, score_t);
        draw_text(10, 96, "RUNS", 1); draw_text(30, 96, buf, 0);
        fmt7(buf, hi_h, hi_t);
        draw_text(72, 96, "BEST", 1); draw_text(92, 96, buf, 0);
    } else {
        draw_text_center(96, "PRESS START", 1);
    }
}

static void draw_over(void)
{
    uint8_t i, f = (frame_ct >> 1) & 1;
    int16_t x, y;
    for (i = 0; i < 3; ++i) {                      /* baseball bats circle the moon */
        x = (int16_t)((((uint16_t)frame_ct * (2 + i)) >> 1) + i * 57) % 140;
        y = 32 + i * 5 + ((frame_ct >> 3) & 3) * (i + 1) / 2 - 12;
        if (x < 116) draw_soft(S_BAT[(f + i) & 1], x, y, 0, 8);
    }
    if (new_best && (frame_ct & 16)) {
        draw_box(30, 51, 68, 9, COL_INK);
        draw_text_center(53, "NEW BEST SCORE!", 1);
    }
    draw_score_plaque();
}

static void draw_win(void)
{
    static const uint8_t bx[3] = { 22, 106, 64 }, by[3] = { 50, 46, 50 };
    static const uint8_t conf[5] = { 63, COL_FLAME3, 215, COL_WHITE, COL_HAT };
    uint8_t i, k, f, sz, t = frame_ct;
    int16_t x, y;
    for (i = 0; i < 3; ++i) {                       /* fireworks */
        k = (uint8_t)((t >> 1) + i * 21) & 63;
        if (k < 40) {
            f = k / 10;
            sz = soft_sprites[S_BURST[f]].w;
            draw_soft(S_BURST[f], bx[i] - (sz >> 1), by[i] - (sz >> 1) - 12, 0, sz);
        }
    }
    for (i = 0; i < 16; ++i) {                      /* confetti */
        x = 3 + (int16_t)((i * 53 + (t >> 3) * (1 + (i & 1))) % 120);
        y = 8 + (int16_t)((((uint16_t)t * (1 + (i & 3)) >> 1) + i * 11) % 104);
        if (y >= 12) draw_box(x, y - 12, 2, 3, conf[i % 5]);
    }
    if (new_best && (frame_ct & 16)) draw_text_center(34, "NEW BEST SCORE!", 0);
    draw_score_plaque();
}

/* the strip at the top of the screen shows either the score characters or the top 12 rows of the over and win pictures */
static void set_strip_map(uint8_t art)
{
    uint8_t i, k;
    uint16_t c0 = art ? ART_STRIP_CH0 : HUD_CH0;
    for (i = 0; i < HUD_ROWS; ++i) {
        for (k = 0; k < FIELD_CH_COLS; ++k) ((uint16_t *)rowbuf)[k] = c0 + i * FIELD_CH_COLS + k;
        dma_copy(rowbuf, FIELD_CH_COLS * 2, 0, SCREEN_A + (uint16_t)i * FIELD_CH_COLS * 2);
        dma_copy(rowbuf, FIELD_CH_COLS * 2, 0, SCREEN_B + (uint16_t)i * FIELD_CH_COLS * 2);
    }
}

static void load_scene_art(uint8_t sc)             /* attic file -> the picture characters in bank 5 */
{
    if (sc == SC_TITLE) dma_copy28(0x80, 0x10000UL, 0, (uint32_t)TITLE_CH0 << 6, 53248);
    else if (sc == SC_INTRO) dma_copy28(0x80, 6UL << 16, 0, (uint32_t)TITLE_CH0 << 6, 53248);
    else dma_copy28(0x80, (uint32_t)(sc == SC_OVER ? 2 : 3) << 16, 0, (uint32_t)TITLE_CH0 << 6, 59392);
}

/* ----------------------------------------------------------------- banners -- */
/* A banner ("INNING 3 / PLAY BALL!", "PAUSED", ...) is a small picture, 80 x 24 game pixels: ink with a rim line at the top
 * and bottom and one or two lines of text. It is drawn once into the spare sprite-copy memory, cut into characters, and
 * set over the field map. */
#define BW (BAN_TX * 8)
#define BH (BAN_TY * 8)

static void ban_text(uint8_t *img, uint8_t y, const char *str, uint8_t set)
{
    uint8_t n = 0, ch, idx, i, j, col = set ? FONT_FG1 : FONT_FG0;
    uint8_t x;
    while (str[n]) ++n;
    x = (BW - n * 4) >> 1;
    while ((ch = (uint8_t)*str++) != 0) {
        idx = glyph_index(ch);
        if (idx != 255)
            for (j = 0; j < 6; ++j)
                for (i = 0; i < 4; ++i)
                    if (font_bits[idx][(j * 4 + i) >> 3] & (1 << ((j * 4 + i) & 7))) img[(y + j) * BW + x + i] = col;
        x += 4;
    }
}

void render_banner(const char *a, const char *b)
{
    uint8_t *img = (uint8_t *)dyn_shadow;
    uint8_t tx, ty, k, j;
    memset(img, COL_INK, BW * BH);
    memset(img + BW + 2, COL_RIM, BW - 4);
    memset(img + (BH - 2) * BW + 2, COL_RIM, BW - 4);
    if (b) { ban_text(img, 5, a, 1); ban_text(img, 14, b, 0); }
    else ban_text(img, 9, a, 1);
    for (ty = 0; ty < BAN_TY; ++ty)
        for (tx = 0; tx < BAN_TX; ++tx)
            for (k = 0; k < SY; ++k) {
                for (j = 0; j < 8; ++j)
                    memcpy(charbuf + j * 8, img + (ty * 8 + (k * 8 + j) / SY) * BW + tx * 8, 8);
                dma_char_out(charbuf, BANNER_CH0 + (ty * BAN_TX + tx) * SY + k);
            }
    banner_on = 1;
    field_dirty = 1;
}

void render_banner_off(void)
{
    banner_on = 0;
    field_dirty = 1;
}

/* ------------------------------------------------------- popups, the depth marker -- */
/* a kill's points float up from where it happened */
static void draw_popups(void)
{
    uint8_t i, n;
    char buf[8];
    uint16_t v;
    for (i = 0; i < MAXP; ++i) {
        if (!pop_t[i]) continue;
        v = pop_v[i];
        n = 0;                                          /* hundreds, then "00" (bit 15: a loss, with a minus sign) */
        if (v & 0x8000) { buf[n++] = '-'; v &= 0x7FFF; }
        if (v >= 100) buf[n++] = '0' + v / 100;
        if (v >= 10) buf[n++] = '0' + (v / 10) % 10;
        buf[n++] = '0' + v % 10;
        buf[n++] = '0'; buf[n++] = '0'; buf[n] = 0;
        draw_text(8 + pop_x[i] + 4 - (n << 1), pop_y[i] + 1 - (pop_t[i] >> 3), buf, 1);
        --pop_t[i];
    }
}

/* the gauges in both margins carry a gold marker that follows Doug down */
static void draw_depth_marker(void)
{
    draw_box(1, py + 3, 5, 3, 63);
    draw_box(122, py + 3, 5, 3, 63);
}

/* ---------------------------------------------- the introduction screens -- */
extern uint8_t icur;                                    /* main.c: which enemy the introduction is about */
extern uint16_t scene_t;                                /* main.c: ticks on the attract screen */

static const char *const I_TITLE[5] = { "WARNING!", "NEW ARRIVAL!", "INCOMING!", "HEADS UP!", "FINAL INNING!" };
static const char *const I_NAME[5]  = { "THE VUMPIRES", "THE HEATERS", "THE BASEBALL BATS", "THE GROUNDSKEEPER", "MAD SCOTT" };
static const char *const I_WHAT[5]  = { "ARE ATTACKING!", "BREATHE FIRE!", "FLY THROUGH DIRT!", "CLOSES YOUR TUNNELS", "IS COMING FOR YOU!" };
static const char *const A_NAME[5]  = { "VUMPIRE", "HEATER", "BASEBALL BAT", "GROUNDSKEEPER", "MAD SCOTT" };
static const char *const A_WHAT[5]  = { "RAISES THE FALLEN", "BREATHES FIRE", "FLIES THROUGH DIRT", "RAKES TUNNELS SHUT", "BOSS. SIX STRIKES" };

/* enemy kind 0-4 (Vumpire, Heater, Baseball Bat, Groundskeeper, Mad Scott) walking right, at plane position x, y */
static void draw_cast(uint8_t kind, int16_t x, int16_t y)
{
    uint8_t f = (frame_ct >> 2) & 1;
    switch (kind) {
    case 0: draw_soft(S_GRUB[f], x, y, 0, 8); break;
    case 1: draw_soft(S_EMB[f], x, y, 0, 8); break;
    case 2: draw_soft(S_BAT[(frame_ct >> 1) & 1], x, y, 0, 8); break;
    case 3: draw_soft(S_GK[f], x, y, 0, 8); break;
    default: draw_soft(S_MASCOT[f], x, y - 4, 0, 16);
    }
}

/* Every time an inning brings a new kind of enemy, this screen introduces it: its name, then a line of it marches across. */
static void draw_intro(void)
{
    uint8_t t = state_timer, i, p;
    if (state == ST_ATTRACT) {                          /* idle at the title: the cast, one at a time */
        i = (uint8_t)(scene_t / 100);
        if (i > 4) i = 4;
        draw_text_center(16, "MEET THE OPPOSITION", 1);
        draw_cast(i, 14, 46);
        draw_text(34, 44, A_NAME[i], 1);
        draw_text(34, 52, A_WHAT[i], 0);
        return;
    }
    if (t >= 30 || (t > 6 && !((t >> 1) & 1))) draw_text_center(19, I_TITLE[icur], 1);
    if (t > 12) draw_text_center(32, I_NAME[icur], 1);
    if (t > 22) draw_text_center(41, I_WHAT[icur], 1);
    for (i = 0; i < 6; ++i) {
        p = (uint8_t)((frame_ct >> 1) - i * 18) & 127;
        if (p >= 100) continue;
        if (icur == 4 && (i & 1)) continue;             /* Mad Scott is big: every other place */
        draw_cast(icur, 8 + p, 65);
    }
    if (t > 45 && (frame_ct & 32) == 0) draw_text_center(80, "PRESS START", 1);
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
    uint8_t i, want = (state == ST_TITLE) ? SC_TITLE : (state == ST_OVER) ? SC_OVER : (state == ST_WIN) ? SC_WIN :
                         (state == ST_INTRO || state == ST_ATTRACT) ? SC_INTRO : SC_GAME;
    if (want != scene) {
        uint8_t was_art = (scene == SC_OVER || scene == SC_WIN), is_art = (want == SC_OVER || want == SC_WIN);
        if (want != SC_GAME) load_scene_art(want);
        if (is_art != was_art) set_strip_map(is_art);
        scene = want; field_dirty = 1; hud_drawn = 0;
    }
    if (field_dirty) { rebuild_base(); field_dirty = 0; }
    hud_update();
    dyn_n = 0;
    memset(dyn_of, 0xFF, sizeof dyn_of);
    if (scene == SC_TITLE) draw_title();
    else if (scene == SC_OVER) draw_over();
    else if (scene == SC_WIN) draw_win();
    else if (scene == SC_INTRO) draw_intro();
    else {
        draw_depth_marker();
        draw_rocks();
        for (i = 0; i < MAXC; ++i)
            if (c_on[i]) draw_soft(SPR_TOMB, 8 + c_x[i], c_y[i], 0, 8);
        if (gold_on && M(gold_c, gold_r) == 0) draw_soft(SPR_GOLD, 8 + (gold_c << 3), gold_r << 3, 0, 8);
        for (i = 0; i < MAXE; ++i)
            if (e_state[i] != ES_NONE) draw_enemy(i);
        draw_ball();
        draw_doug();
        draw_popups();
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

static void video_regs(void)
{
    POKE(0xD05D, PEEK(0xD05D) & 0x7F);               /* HOTREG off: otherwise the chip recomputes the layout from the VIC-II registers whenever they are touched,
                                                        which on real hardware wipes the settings below */
    POKE(0xD030, PEEK(0xD030) | 0x04);               /* colours 0-15 come from the palette RAM too */
    POKE(0xD070, 0x10 | 0x08);                       /* characters use palette bank 1, sprites bank 2 */
    POKE(0xD020, BLANK_PIXEL); POKE(0xD021, BLANK_PIXEL);
    POKE(0xD031, PEEK(0xD031) | 0x88);               /* H640 + V400: 640x400. (Writing $D031 resets the registers below.) */
    POKE(0xD054, 0x05);                              /* CHR16 (13-bit character numbers) + full-colour for chars > $FF */
    POKE(0xD05B, 0x00);                              /* one raster per character line (400-line mode). The chip sets this itself when it recomputes the layout, which HOTREG off prevents; the 200-line value (1) draws everything twice as tall */
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
    POKE(0xD05D, PEEK(0xD05D) & 0x40);
}

void render_show(void)                               /* the first picture is in place: show it */
{
    POKE(0xD011, PEEK(0xD011) | 0x10);
}

void render_init(void)
{
    uint8_t i, n, k;
    POKE(0xD02F, 0x47); POKE(0xD02F, 0x53);          /* unlock the VIC-IV registers */
    POKE(0xD05D, PEEK(0xD05D) & 0x7F);               /* (HOTREG off first, see video_regs) */
    POKE(0xD011, PEEK(0xD011) & 0xEF);               /* blank the screen: the mode is switched on a second before there is a picture to show */
    POKE(0xD06F, PEEK(0xD06F) | 0x80);               /* 60 Hz (NTSC) timing, so two frames are exactly one 30 Hz game tick */
    wait_frame(); wait_frame(); wait_frame();        /* let the mode change settle before the registers that it resets are set */
    POKE(0xD030, PEEK(0xD030) | 0x04);               /* colours 0-15 come from the palette RAM too */
    /* palette bank 1 (mapped in) is for the characters */
    set_palette(0x40 | 0x10 | 0x08, palette_rgb, 256);
    POKE(0xD070, 0x10 | 0x08);                       /* characters use bank 1, sprites bank 2 */
    POKE(0xD020, BLANK_PIXEL); POKE(0xD021, BLANK_PIXEL);
    for (i = 0, n = 0; i < 60 && n < 2; ++i) {       /* the chip may apply the 60 Hz switch late and reset its registers when it does (real */
        video_regs();                                /* hardware does; Xemu does not): set them again until they have stayed set twice running */
        for (k = 0; k < 30; ++k) wait_frame();
        if (PEEK(0xD05E) == FIELD_CH_COLS && PEEK(0xD04C) == (uint8_t)FIELD_X && PEEK(0xD048) == (uint8_t)TEXT_Y) ++n; else n = 0;
    }

    build_tiles();
    dma_copy28(0x80, 5UL << 16, 0, 0x4E000UL, GLYPH_OFF);                              /* the sprite pixels: attic file 5 -> chip RAM */
    dma_copy28(0x80, (5UL << 16) + GLYPH_OFF, 0, 0x5E800UL, SPRITE_BLOCK_SIZE - GLYPH_OFF);   /* and the glyph images */
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
