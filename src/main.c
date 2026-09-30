/*
 * Dug Out for the MEGA65.
 *
 * The VIC-IV shows the field as a map of full-colour 8x8 characters. Each game tile is three characters stacked (its pixel
 * rows repeated 3x, so it is 3x tall) and the hardware stretches the characters 4x wide (the screen's pixels are taller
 * than they are wide, so 4x3 looks square). Digging a cell swaps the three characters in it. Doug and the enemies are
 * full-colour hardware sprites drawn at the same scale. Character n lives at chip RAM address 64*n, so the tile set
 * starts at character 4096 ($40000).
 */
#include <stdint.h>
#include "data.h"

#define POKE(a, v) (*(volatile uint8_t *)(a) = (uint8_t)(v))
#define PEEK(a)    (*(volatile uint8_t *)(a))

/* ------------------------------------------------------------------ DMA -- */
static uint8_t dmalist[16];

/* One enhanced DMA job: copy (cmd 0) or fill (cmd 3). Source is in the first 64K of RAM (copy) or a fill value;
 * the destination is a 28-bit address, given as MB (bits 27-20) and a 20-bit offset. */
static void dma_job(uint8_t cmd, uint16_t count, uint16_t src, uint8_t dst_mb, uint32_t dst)
{
    dmalist[0] = 0x0B;                     /* 12-byte list format */
    dmalist[1] = 0x81;                     /* destination MB follows */
    dmalist[2] = dst_mb;
    dmalist[3] = 0x00;                     /* end of options */
    dmalist[4] = cmd;
    dmalist[5] = (uint8_t)count;
    dmalist[6] = (uint8_t)(count >> 8);
    dmalist[7] = (uint8_t)src;             /* for a fill this is the fill value */
    dmalist[8] = (uint8_t)(src >> 8);
    dmalist[9] = 0x00;                     /* source bank */
    dmalist[10] = (uint8_t)dst;
    dmalist[11] = (uint8_t)(dst >> 8);
    dmalist[12] = (uint8_t)((dst >> 16) & 0x0F);
    dmalist[13] = 0x00;
    dmalist[14] = 0x00;
    dmalist[15] = 0x00;
    POKE(0xD702, 0x00);                    /* the list is in bank 0 */
    POKE(0xD701, (uint16_t)dmalist >> 8);
    POKE(0xD705, (uint8_t)(uint16_t)dmalist);   /* writing the LSB here runs an enhanced job */
}

#define dma_copy(src, count, dst_mb, dst) dma_job(0x00, (count), (uint16_t)(src), (dst_mb), (dst))
#define dma_fill(val, count, dst_mb, dst) dma_job(0x03, (count), (val), (dst_mb), (dst))

/* ---------------------------------------------------------------- level -- */
#define COLS 14
#define ROWS 13

static uint8_t map[ROWS][COLS];       /* 0 = tunnel, 1 = dirt */

static uint8_t solid(int8_t c, int8_t r)
{
    if (c < 0 || c >= COLS || r < 0 || r >= ROWS) return 1;
    return map[r][c] != 0;
}

static void carve(uint8_t c, uint8_t r, uint8_t w, uint8_t h)
{
    uint8_t i, j;
    for (j = 0; j < h; ++j)
        for (i = 0; i < w; ++i) map[r + j][c + i] = 0;
}

static void build_test_level(void)
{
    uint8_t r, c;
    for (r = 0; r < ROWS; ++r)
        for (c = 0; c < COLS; ++c) map[r][c] = (r == 0) ? 0 : 1;
    carve(6, 1, 1, 2);                /* Doug's shaft */
    carve(0, 4, 5, 1); carve(2, 5, 1, 1);
    carve(8, 6, 5, 1); carve(10, 7, 1, 2);
    carve(4, 10, 5, 1); carve(6, 11, 1, 1);
}

static uint8_t tunnel_mask(uint8_t c, uint8_t r)
{
    uint8_t m = 0;
    if (solid(c, (int8_t)r - 1)) m |= 1;
    if (solid((int8_t)c + 1, r)) m |= 2;
    if (solid(c, (int8_t)r + 1)) m |= 4;
    if (c == 0 || solid((int8_t)c - 1, r)) m |= 8;
    return m;
}

/* -------------------------------------------------------------- display -- */
#define CHAR_BASE    4096u                   /* first tile character: chip RAM $40000 */
#define TUNNEL_TILE0 (FIELD_CH_COLS * FIELD_CH_ROWS)   /* tiles 208..223 are the tunnel tiles */
#ifndef XSCL
#define XSCL 30      /* character width is about 980/XSCL physical pixels (rounded down): 30 gives 32, i.e. 4x (measured in Xemu) */
#endif
#define SCREEN_ROWS  (FIELD_CH_ROWS * 3)
#define SCREEN_RAM   0x12000UL               /* 2 bytes per character */
#define COLOUR_OFS   0x2000u                 /* colour RAM offset, in $FF80000 */
#define SPRITE_PTRS  0x15F00UL               /* the sprite pointer list (16 bytes) */
#define SPRITE_DATA  0x16000UL               /* sprite images, 64-byte aligned */

/* Layout on the 640x400 display, in physical pixels, all measured in Xemu. The window is x 80..720, y 104..504.
 * The picture is 512x312 (16 characters of 32 pixels, 13 rows of 24), so to centre it the text area starts at 144,148.
 * A game pixel is 4 wide and 3 tall on screen. A sprite at register (X, Y) has its left edge at screen x = 2X+31 and
 * its top at screen y = Y (sprite pixels are 2 physical pixels wide). */
#define FIELD_X      144
#define FIELD_Y      148
#define SCR_X(gx)    (FIELD_X + 32 + (gx) * 4)            /* game x (0 = left edge of cell 0) -> screen x */
#define SCR_Y(gy)    (FIELD_Y + (gy) * 3)
#define SPR_REG_X(sx) (((sx) - 31) / 2)

static uint8_t rowbuf[FIELD_CH_COLS * 2];
static uint8_t charbuf[64];

static void set_palette(uint8_t bank_sel, const uint8_t (*rgb)[3], uint16_t first, uint16_t n)
{
    uint16_t i;
    POKE(0xD070, bank_sel);
    for (i = 0; i < n; ++i) {
        uint8_t r = rgb[i][0], g = rgb[i][1], b = rgb[i][2];
        POKE(0xD100 + first + i, (r << 4) | (r >> 4));      /* palette values are stored with their nybbles swapped */
        POKE(0xD200 + first + i, (g << 4) | (g >> 4));
        POKE(0xD300 + first + i, (b << 4) | (b >> 4));
    }
}

/* one 8x8 tile becomes three characters: character k holds the tile's pixel rows repeated 3x */
static void make_tile(uint16_t tile, const uint8_t *px)
{
    uint8_t k, py, i;
    for (k = 0; k < 3; ++k) {
        for (py = 0; py < 8; ++py) {
            const uint8_t *row = px + ((k * 8 + py) / 3) * 8;
            for (i = 0; i < 8; ++i) charbuf[py * 8 + i] = row[i];
        }
        dma_copy(charbuf, 64, 0, ((uint32_t)(CHAR_BASE + tile * 3 + k)) << 6);
    }
}

static void build_tiles(void)
{
    uint16_t t;
    for (t = 0; t < FIELD_CH_COLS * FIELD_CH_ROWS; ++t) make_tile(t, field_tiles[t]);
    for (t = 0; t < 16; ++t) make_tile(TUNNEL_TILE0 + t, tunnel_tiles[t]);
}

static void draw_field(void)
{
    uint8_t r, c, cell, sub;
    uint16_t tile, ch;
    for (r = 0; r < FIELD_CH_ROWS; ++r) {
        for (sub = 0; sub < 3; ++sub) {
            for (c = 0; c < FIELD_CH_COLS; ++c) {
                tile = (uint16_t)r * FIELD_CH_COLS + c;                    /* the dirt picture */
                cell = c - 1;
                if (r >= 1 && c >= 1 && c <= COLS && map[r][cell] == 0)
                    tile = TUNNEL_TILE0 + tunnel_mask(cell, r);             /* a dug cell: the tunnel tile for its neighbours */
                ch = CHAR_BASE + tile * 3 + sub;
                rowbuf[c * 2] = (uint8_t)ch;
                rowbuf[c * 2 + 1] = (uint8_t)(ch >> 8);
            }
            dma_copy(rowbuf, FIELD_CH_COLS * 2, 0, SCREEN_RAM + (uint32_t)(r * 3 + sub) * (FIELD_CH_COLS * 2));
        }
    }
}

static void video_init(void)
{
    POKE(0xD02F, 0x47); POKE(0xD02F, 0x53);          /* unlock the VIC-IV registers */
    POKE(0xD030, PEEK(0xD030) | 0x04);               /* colours 0-15 come from the palette RAM too */
    /* palette bank 1 (mapped in) is for the characters; bank 2 is for the sprites */
    set_palette(0x40 | 0x10 | 0x08, palette_rgb, 0, 256);
    set_palette(0x80 | 0x10 | 0x08, doug_pal, 0, 16);
    POKE(0xD070, 0x10 | 0x08);                       /* characters use bank 1, sprites bank 2 */
    POKE(0xD020, BLANK_PIXEL); POKE(0xD021, BLANK_PIXEL);
    POKE(0xD031, PEEK(0xD031) | 0x88);               /* H640 + V400: 640x400. (Writing $D031 resets the registers below.) */
    POKE(0xD054, 0x05);                              /* CHR16 (13-bit character numbers) + full-colour for chars > $FF */
    POKE(0xD05A, XSCL);                              /* characters 4x wide */
    POKE(0xD05E, FIELD_CH_COLS);                     /* characters per row */
    POKE(0xD058, FIELD_CH_COLS * 2); POKE(0xD059, 0);/* bytes per row */
    POKE(0xD07B, SCREEN_ROWS - 1);                   /* rows, minus one */
    POKE(0xD060, (uint8_t)SCREEN_RAM);
    POKE(0xD061, (uint8_t)(SCREEN_RAM >> 8));
    POKE(0xD062, (uint8_t)(SCREEN_RAM >> 16));
    POKE(0xD063, PEEK(0xD063) & 0xF0);
    POKE(0xD064, (uint8_t)COLOUR_OFS);
    POKE(0xD065, (uint8_t)(COLOUR_OFS >> 8));
    POKE(0xD04C, (uint8_t)FIELD_X);                  /* text area start: centre the picture in the window */
    POKE(0xD04D, (PEEK(0xD04D) & 0xF0) | (FIELD_X >> 8));
    POKE(0xD04E, (uint8_t)FIELD_Y);
    POKE(0xD04F, (PEEK(0xD04F) & 0xF0) | (FIELD_Y >> 8));
}

static void sprites_init(void)
{
    dma_copy(doug_frames, (uint16_t)DOUG_FRAMES * SPRITE_BYTES, 0, SPRITE_DATA);      /* every Doug frame, 192 bytes each */
    POKE(0xD06C, (uint8_t)SPRITE_PTRS);
    POKE(0xD06D, (uint8_t)(SPRITE_PTRS >> 8));
    POKE(0xD06E, 0x80 | (uint8_t)(SPRITE_PTRS >> 16));   /* 16-bit sprite pointers, list at $15F00 */
    POKE(0xD06B, 0x01);                              /* sprite 0: full colour (16 pixels wide) */
    POKE(0xD027, 0x00);                              /* in full-colour mode this register's low nybble is the transparent pixel value */
    POKE(0xD055, 0x01); POKE(0xD056, 24);            /* sprite 0 is 24 rows tall */
    POKE(0xD076, 0x01);                              /* native vertical resolution for sprite 0 */
    POKE(0xD015, 0x01);                              /* sprite 0 on */
}

/* show Doug at game pixel (gx, gy), using image `frame` */
static void show_doug(uint8_t gx, uint8_t gy, uint8_t frame)
{
    uint16_t ptr = (uint16_t)(SPRITE_DATA >> 6) + (uint16_t)frame * (SPRITE_BYTES / 64);
    uint16_t sx = SPR_REG_X(SCR_X(gx)), sy = SCR_Y(gy);
    rowbuf[0] = (uint8_t)ptr; rowbuf[1] = (uint8_t)(ptr >> 8);
    dma_copy(rowbuf, 2, 0, SPRITE_PTRS);
    POKE(0xD000, (uint8_t)sx);
    POKE(0xD010, (sx >> 8) ? 0x01 : 0x00);
    POKE(0xD001, (uint8_t)sy);
    POKE(0xD077, (sy >> 8) ? 0x01 : 0x00);           /* top bit of sprite 0's Y */
}

/* ---------------------------------------------------------------- input -- */
#define IN_UP    1
#define IN_DOWN  2
#define IN_LEFT  4
#define IN_RIGHT 8
#define IN_FIRE  16
#define IN_START 32

static uint8_t key_down(uint8_t col, uint8_t row)
{
    POKE(0xDC00, (uint8_t)~(1u << col));
    return (PEEK(0xDC01) & (1u << row)) == 0;
}

static uint8_t read_input(void)
{
    uint8_t r = 0, shift, j;
    POKE(0xDC02, 0xFF);                               /* port A drives the keyboard columns */
    POKE(0xDC03, 0x00);
    shift = key_down(1, 7) | key_down(6, 4);
    if (key_down(0, 7)) r |= shift ? IN_UP : IN_DOWN;           /* cursor down / up */
    if (key_down(0, 2)) r |= shift ? IN_LEFT : IN_RIGHT;        /* cursor right / left */
    if (key_down(1, 4)) r |= IN_FIRE;                           /* Z */
    if (key_down(0, 1)) r |= IN_START;                          /* Return */
    POKE(0xDC00, 0xFF);
    POKE(0xDC02, 0x00);                               /* port A back to input: joystick port 2 */
    j = (uint8_t)~PEEK(0xDC00);
    if (j & 0x01) r |= IN_UP;
    if (j & 0x02) r |= IN_DOWN;
    if (j & 0x04) r |= IN_LEFT;
    if (j & 0x08) r |= IN_RIGHT;
    if (j & 0x10) r |= IN_FIRE;
    return r;
}

/* ---------------------------------------------------------------- player -- */
enum { DIR_R, DIR_L, DIR_U, DIR_D };
static const int8_t DX[4] = { 1, -1, 0, 0 };
static const int8_t DY[4] = { 0, 0, -1, 1 };
static const uint8_t LEADX[4] = { 8, 0, 4, 4 };
static const uint8_t LEADY[4] = { 4, 4, 0, 8 };

static uint8_t px, py, pdir, panim, tick_ct;

static uint8_t can_move(uint8_t x, uint8_t y, uint8_t d)
{
    switch (d) {
    case DIR_R: return x < 104;
    case DIR_L: return x != 0;
    case DIR_U: return y != 0;
    default:    return y < 96;
    }
}

static void reset_player(void)
{
    px = 6 << 3; py = 2 << 3; pdir = DIR_D; panim = 0;
    carve(6, 1, 1, 2);
}

/* one game tick: the GameTank version's movement and digging rules */
static void player_update(uint8_t in)
{
    uint8_t want = 255, misal, d, moved = 0, c, r, slow;

    if (pdir < 2) {
        if (in & IN_UP) want = DIR_U;
        else if (in & IN_DOWN) want = DIR_D;
        else if (in & IN_LEFT) want = DIR_L;
        else if (in & IN_RIGHT) want = DIR_R;
    } else {
        if (in & IN_LEFT) want = DIR_L;
        else if (in & IN_RIGHT) want = DIR_R;
        else if (in & IN_UP) want = DIR_U;
        else if (in & IN_DOWN) want = DIR_D;
    }

    if (want != 255) {
        if ((want >> 1) != (pdir >> 1)) {
            misal = (want < 2) ? (py & 7) : (px & 7);        /* turning a corner: slide onto the grid first */
            if (misal) {
                if (want < 2) d = (misal <= 4) ? DIR_U : DIR_D;
                else          d = (misal <= 4) ? DIR_L : DIR_R;
                if (can_move(px, py, d)) { px += DX[d]; py += DY[d]; moved = 1; }
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
        slow = (r < ROWS && c < COLS && map[r][c] == 1);     /* digging through dirt is half speed */
        if (!slow || (tick_ct & 1)) { px += DX[pdir]; py += DY[pdir]; moved = 1; }
    }

    if (moved) {
        ++panim;
        c = (px + 4) >> 3; r = (py + 4) >> 3;
        if (r < ROWS && c < COLS && map[r][c] == 1) {
            map[r][c] = 0;                                   /* dig */
            draw_field();
        }
    }
}

static void wait_frame(void)
{
    while (PEEK(0xD012) != 0xFF) { }
    while (PEEK(0xD012) == 0xFF) { }
}

int main(void)
{
#ifdef TEST_EXIT
    uint16_t n;
#endif
    uint8_t in;
    __asm__("sei");                                  /* the ROM's interrupt routine would fight over the screen */
    POKE(0x00, 65);                                  /* 40 MHz */
    build_test_level();
    reset_player();
    video_init();
    build_tiles();
    dma_fill(0, FIELD_CH_COLS * SCREEN_ROWS * 2, 0xFF, 0x80000UL + COLOUR_OFS);      /* colour RAM: plain characters */
    draw_field();
    sprites_init();
    show_doug(px, py, pdir * 2);
#ifdef TEST_EXIT
    /* no keyboard in a test run: dig down, then right along row 4's pocket level, then down again */
    for (n = 0; n < 420; ++n) {
#else
    for (;;) {
#endif
        wait_frame(); wait_frame();                  /* about 25 ticks a second on PAL */
        ++tick_ct;
#ifdef TEST_EXIT
        in = (n < 130) ? IN_DOWN : (n < 250) ? IN_RIGHT : IN_DOWN;
#else
        in = read_input();
#endif
        player_update(in);
        show_doug(px, py, pdir * 2 + ((panim >> 2) & 1));
    }
#ifdef TEST_EXIT
    POKE(0xD6CF, 0x42);                              /* Xemu in -testing mode exits when this is written */
    for (;;) { }
#endif
}
