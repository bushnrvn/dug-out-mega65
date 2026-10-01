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
#define SCREEN_ROWS  (FIELD_CH_ROWS * 3)
#define HUD_ROWS     6                       /* the score strip above the field: 16 game pixels = 6 character rows */
#define HUD_CHARS    (FIELD_CH_COLS * HUD_ROWS)
#define HUD_CH0      5400u                   /* its characters (they never change places, only pixels) */
#define SCREEN_CHARS (FIELD_CH_COLS * SCREEN_ROWS)
#define TOTAL_CHARS  (HUD_CHARS + SCREEN_CHARS)
#define FIELD_OFS    (HUD_CHARS * 2)         /* byte offset of the field's rows inside a screen buffer */
#define DYN_MAX      96                      /* characters that can be unique to one frame */
#define POOL_A       5000u                   /* their character numbers: two pools, one per screen buffer */
#define POOL_B       (POOL_A + DYN_MAX)
#define SCREEN_A     0x12000UL               /* two screen buffers, 2 bytes per character */
#define SCREEN_B     0x13000UL
#define COLOUR_OFS   0x2000u                 /* colour RAM offset, in $FF80000 */
#define SPRITE_PTRS  0x15F00UL               /* Doug's sprite pointer list (16 bytes) */
#define SPRITE_DATA  0x16000UL               /* Doug's sprite images, 64-byte aligned */
#define XSCL         30                      /* character width = about 980/XSCL pixels: 30 gives 32, i.e. 4x (measured in Xemu) */

/* Layout on the 640x400 display (window x 80..720, y 104..504), in physical pixels, all measured in Xemu.
 * The picture is 512x360 (strip and field), so to centre it the text area starts at 144,124. A sprite at register (X, Y) has its left edge at
 * screen x = 2X+31 and its top at screen y = Y. */
#define FIELD_X      144
#define TEXT_Y       124                                  /* top of the strip: the 512x360 picture is centred in 400 rows */
#define FIELD_Y      (TEXT_Y + HUD_ROWS * 8)
#define SCR_X(gx)    (FIELD_X + 32 + (gx) * 4)            /* game x (0 = left edge of cell 0) -> screen x */
#define SCR_Y(gy)    (FIELD_Y + (gy) * 3)
#define SPR_REG_X(sx) (((sx) - 31) / 2)

static uint16_t base_scr[SCREEN_ROWS][FIELD_CH_COLS];   /* the plain map: dirt and tunnels */
static uint16_t scr[SCREEN_ROWS][FIELD_CH_COLS];        /* this frame's map, with the copies in it */
static int8_t dyn_of[SCREEN_ROWS][FIELD_CH_COLS];       /* which copy a cell has this frame, or -1 */
static uint16_t dyn_cell[DYN_MAX];                      /* the cell of each copy: row * 16 + column */
static uint8_t dyn_n;
static uint8_t dyn_shadow[DYN_MAX][64];                 /* the copies, in normal memory while they are drawn into */
static uint8_t dyn_dummy[64];
static uint8_t tilebuf[64], charbuf[64], rowbuf[FIELD_CH_COLS * 2];
static uint8_t cur_buf;                                 /* the buffer being drawn (not the one being shown) */

/* ------------------------------------------------------------------ tiles -- */
static void tile_px(uint16_t tile, uint8_t *dst)
{
    if (tile < N_BIG)            dma_copy(field_tiles_a[tile], 64, 0, (uint32_t)(uint16_t)dst);   /* above $D000: DMA only */
    else if (tile < N_FIELD)     memcpy(dst, field_tiles_b[tile - N_BIG], 64);
    else                         memcpy(dst, tunnel_tiles[tile - N_FIELD], 64);
}

/* character k (0..2) of a tile: the tile's pixel rows repeated 3x */
static void tile_char(uint16_t tile, uint8_t k, uint8_t *dst)
{
    uint8_t py, i;
    tile_px(tile, tilebuf);
    for (py = 0; py < 8; ++py) {
        const uint8_t *row = tilebuf + ((k * 8 + py) / 3) * 8;
        for (i = 0; i < 8; ++i) dst[py * 8 + i] = row[i];
    }
}

static void build_tiles(void)
{
    uint16_t t;
    uint8_t k;
    for (t = 0; t < N_TILES; ++t) {
        tile_px(t, tilebuf);
        for (k = 0; k < 3; ++k) {
            uint8_t py, i;
            for (py = 0; py < 8; ++py) {
                const uint8_t *row = tilebuf + ((k * 8 + py) / 3) * 8;
                for (i = 0; i < 8; ++i) charbuf[py * 8 + i] = row[i];
            }
            dma_copy(charbuf, 64, 0, ((uint32_t)(CHAR_BASE + t * 3 + k)) << 6);
        }
    }
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

static void rebuild_base(void)
{
    uint8_t rr, cc, k;
    uint16_t tile;
    for (rr = 0; rr < FIELD_CH_ROWS; ++rr)
        for (cc = 0; cc < FIELD_CH_COLS; ++cc) {
            tile = tile_at(cc, rr);
            for (k = 0; k < 3; ++k) base_scr[rr * 3 + k][cc] = CHAR_BASE + tile * 3 + k;
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
        tile_char(tile_at(cc, cr / 3), cr % 3, dyn_shadow[d]);   /* start from the plain tile */
    }
    return dyn_shadow[d];
}

/* one game pixel at plane position (X, Y) (X 0..127, Y 0..103): one character pixel across, three down */
static void plot(int16_t X, int16_t Y, uint8_t v)
{
    uint16_t R;
    uint8_t cc, cx, t;
    if (X < 0 || X >= 128 || Y < 0 || Y >= 104) return;
    cc = (uint8_t)X >> 3; cx = (uint8_t)X & 7;
    for (t = 0; t < 3; ++t) {
        R = 3 * (uint16_t)Y + t;
        dyn_for(cc, (uint8_t)(R >> 3))[(R & 7) * 8 + cx] = v;
    }
}

static void draw_box(int16_t X, int16_t Y, uint8_t w, uint8_t h, uint8_t v)
{
    uint8_t i, j;
    for (j = 0; j < h; ++j)
        for (i = 0; i < w; ++i) plot(X + i, Y + j, v);
}

/* rows r0..r0+rows-1 of a sprite, with its top-left at plane position (X, Y) */
static void draw_soft(uint8_t idx, int16_t X, int16_t Y, uint8_t r0, uint8_t rows)
{
    const SoftSprite *s = &soft_sprites[idx];
    uint8_t w = s->w, h = s->h, sx, sy, n, b;
    uint16_t i;
    if ((uint8_t)(r0 + rows) < h) h = r0 + rows;
    for (sy = r0; sy < h; ++sy)
        for (sx = 0; sx < w; ++sx) {
            i = (uint16_t)sy * w + sx;
            b = s->px[i >> 1];
            n = (i & 1) ? (b & 15) : (b >> 4);
            if (n) plot(X + sx, Y + sy, s->pal[n]);
        }
}

static void commit_and_show(void)
{
    uint8_t d;
    uint16_t cell, pool = cur_buf ? POOL_B : POOL_A;
    dma_copy(base_scr, SCREEN_CHARS * 2, 0, (uint32_t)(uint16_t)scr);
    for (d = 0; d < dyn_n; ++d) {
        cell = dyn_cell[d];
        dma_copy(dyn_shadow[d], 64, 0, ((uint32_t)(pool + d)) << 6);
        scr[cell >> 4][cell & 15] = pool + d;
    }
    dma_copy(scr, SCREEN_CHARS * 2, 0, (cur_buf ? SCREEN_B : SCREEN_A) + FIELD_OFS);
    wait_frame();
    POKE(0xD061, cur_buf ? (uint8_t)(SCREEN_B >> 8) : (uint8_t)(SCREEN_A >> 8));        /* show it */
    POKE(0xD062, cur_buf ? (uint8_t)(SCREEN_B >> 16) : (uint8_t)(SCREEN_A >> 16));
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

/* ------------------------------------------------------------ Doug's sprite -- */
static void show_doug(void)
{
    uint8_t frame = pdir * 2 + (pmoving ? ((panim >> 2) & 1) : 0);
    uint16_t ptr = (uint16_t)(SPRITE_DATA >> 6) + (uint16_t)frame * (SPRITE_BYTES / 64);
    uint16_t sx = SPR_REG_X(SCR_X(px)), sy = SCR_Y(py);
    rowbuf[0] = (uint8_t)ptr; rowbuf[1] = (uint8_t)(ptr >> 8);
    dma_copy(rowbuf, 2, 0, SPRITE_PTRS);
    POKE(0xD000, (uint8_t)sx);
    POKE(0xD010, (sx >> 8) ? 0x01 : 0x00);
    POKE(0xD001, (uint8_t)sy);
    POKE(0xD077, (sy >> 8) ? 0x01 : 0x00);           /* top bit of sprite 0's Y */
}

/* ------------------------------------------------------------------- HUD -- */
/* The strip is drawn into a 128x16 picture (one byte per game pixel, in the sprite copies' spare memory), then cut into its 96 characters. It only
 * changes when the score, lives or inning do. */
#define hud_pic ((uint8_t (*)[128])dyn_shadow)          /* the sprite copies are free at this point of a frame */
static uint8_t hud_lives, hud_level, hud_drawn;
static uint16_t hud_score_h; static uint8_t hud_score_t;

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
    uint8_t i, j, n, cx, cy, py, px;
    uint16_t v;
    char buf[8];
    memset(hud_pic, COL_INK, sizeof hud_pic);
    memset(hud_pic[15], COL_RIM, 128);
    v = score_h;
    for (i = 5; i > 0; --i) { buf[i - 1] = '0' + (v % 10); v /= 10; }
    buf[5] = '0' + score_t; buf[6] = '0'; buf[7] = 0;
    hud_text(10, 5, "RUNS", 1);
    hud_text(30, 5, buf, 0);
    hud_text(62, 5, "INN", 1);
    buf[0] = '0' + (level / 10) % 10; buf[1] = '0' + level % 10; buf[2] = 0;
    hud_text(74, 5, buf, 0);
    for (n = 0; n < lives && n < 5; ++n)
        for (j = 0; j < 6; ++j)
            for (i = 0; i < 8; ++i)
                if (life_px[j * 8 + i]) hud_pic[5 + j][121 - (n << 3) - 8 + i] = life_px[j * 8 + i];
    /* each character row is 8 screen rows = 8/3 game rows */
    for (cy = 0; cy < HUD_ROWS; ++cy)
        for (cx = 0; cx < FIELD_CH_COLS; ++cx) {
            for (py = 0; py < 8; ++py)
                for (px = 0; px < 8; ++px)
                    charbuf[py * 8 + px] = hud_pic[(cy * 8 + py) / 3][cx * 8 + px];
            dma_copy(charbuf, 64, 0, ((uint32_t)(HUD_CH0 + cy * FIELD_CH_COLS + cx)) << 6);
        }
    hud_score_h = score_h; hud_score_t = score_t; hud_lives = lives; hud_level = level; hud_drawn = 1;
}

static void hud_update(void)
{
    if (!hud_drawn || score_h != hud_score_h || score_t != hud_score_t || lives != hud_lives || level != hud_level)
        hud_build();
}

void render_frame(void)
{
    uint8_t i;
    if (field_dirty) { rebuild_base(); field_dirty = 0; }
    hud_update();
    dyn_n = 0;
    memset(dyn_of, 0xFF, sizeof dyn_of);
    draw_rocks();
    for (i = 0; i < MAXC; ++i)
        if (c_on[i]) draw_soft(SPR_TOMB, 8 + c_x[i], c_y[i], 0, 8);
    for (i = 0; i < MAXE; ++i)
        if (e_state[i] != ES_NONE) draw_enemy(i);
    draw_ball();
    show_doug();
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

static uint16_t hud_map[HUD_CHARS];

void render_init(void)
{
    uint8_t i;
    POKE(0xD02F, 0x47); POKE(0xD02F, 0x53);          /* unlock the VIC-IV registers */
    POKE(0xD030, PEEK(0xD030) | 0x04);               /* colours 0-15 come from the palette RAM too */
    /* palette bank 1 (mapped in) is for the characters; bank 2 is for the sprites */
    set_palette(0x40 | 0x10 | 0x08, palette_rgb, 256);
    set_palette(0x80 | 0x10 | 0x08, doug_pal, 16);
    POKE(0xD070, 0x10 | 0x08);                       /* characters use bank 1, sprites bank 2 */
    POKE(0xD020, BLANK_PIXEL); POKE(0xD021, BLANK_PIXEL);
    POKE(0xD031, PEEK(0xD031) | 0x88);               /* H640 + V400: 640x400. (Writing $D031 resets the registers below.) */
    POKE(0xD054, 0x05);                              /* CHR16 (13-bit character numbers) + full-colour for chars > $FF */
    POKE(0xD05A, XSCL);                              /* characters 4x wide */
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

    build_tiles();
    dma_fill(0, TOTAL_CHARS * 2, 0xFF, 0x80000UL + COLOUR_OFS);       /* colour RAM: plain characters */

    /* Doug: every frame, then sprite 0 */
    dma_copy(doug_frames, (uint16_t)DOUG_FRAMES * SPRITE_BYTES, 0, SPRITE_DATA);
    POKE(0xD06C, (uint8_t)SPRITE_PTRS);
    POKE(0xD06D, (uint8_t)(SPRITE_PTRS >> 8));
    POKE(0xD06E, 0x80 | (uint8_t)(SPRITE_PTRS >> 16));   /* 16-bit sprite pointers, list at $15F00 */
    POKE(0xD06B, 0x01);                              /* sprite 0: full colour (16 pixels wide) */
    POKE(0xD027, 0x00);                              /* in full-colour mode this register's low nybble is the transparent pixel value */
    POKE(0xD055, 0x01); POKE(0xD056, 24);            /* sprite 0 is 24 rows tall */
    POKE(0xD076, 0x01);                              /* native vertical resolution for sprite 0 */
    POKE(0xD015, 0x01);                              /* sprite 0 on */

    for (i = 0; i < HUD_CHARS; ++i) { hud_map[i] = HUD_CH0 + i; }
    dma_copy(hud_map, HUD_CHARS * 2, 0, SCREEN_A);
    dma_copy(hud_map, HUD_CHARS * 2, 0, SCREEN_B);
    field_dirty = 1;
    rebuild_base();
    field_dirty = 0;
    cur_buf = 0;
}
