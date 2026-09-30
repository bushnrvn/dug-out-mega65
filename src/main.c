/*
 * Dug Out for the MEGA65 - milestone 1: the dirt field on screen.
 *
 * The VIC-IV is put into 80x50 full-colour text mode with 13-bit character numbers. The game's dirt picture and its
 * tunnel tiles become 8x8 characters, enlarged 3x at start-up (each game cell is 3x3 characters).
 * Character n lives at chip RAM address 64*n, so the tile set starts at character 4096 ($40000).
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
#define SCALE        3                       /* each game pixel is 3x3 screen pixels */
#define TILE_CHARS   (SCALE * SCALE)         /* characters per enlarged 8x8 tile */
#define CHAR_BASE    4096u                   /* first tile character: chip RAM $40000 */
#define N_FIELD      (FIELD_CH_COLS * FIELD_CH_ROWS)
#define TUNNEL_TILE0 N_FIELD                 /* tile numbers 208..223 are the tunnel tiles */
#define BLANK_TILE   (N_FIELD + 16)          /* and one plain tile for the margins */
#define N_TILES      (BLANK_TILE + 1)

#define SCREEN_COLS  80
#define SCREEN_ROWS  50
#define FIELD_X0     ((SCREEN_COLS - FIELD_CH_COLS * SCALE) / 2)
#define FIELD_Y0     ((SCREEN_ROWS - FIELD_CH_ROWS * SCALE) / 2)

#define SCREEN_RAM   0x12000UL               /* 2 bytes per character; bank 1, clear of the tile graphics at $40000-$5FFFF */
#define COLOUR_OFS   0x2000u                 /* colour RAM offset, in $FF80000 */

static uint8_t charbuf[64];
static uint8_t rowbuf[SCREEN_COLS * 2];

static void set_palette(void)
{
    uint16_t i;
    for (i = 0; i < 256; ++i) {
        uint8_t r = palette_rgb[i][0], g = palette_rgb[i][1], b = palette_rgb[i][2];
        POKE(0xD100 + i, (r << 4) | (r >> 4));      /* palette values are stored with their nybbles swapped */
        POKE(0xD200 + i, (g << 4) | (g >> 4));
        POKE(0xD300 + i, (b << 4) | (b >> 4));
    }
}

/* enlarge one 8x8 tile into SCALE*SCALE characters in chip RAM */
static void make_tile(uint8_t tile, const uint8_t *px)
{
    uint8_t cy, cx, py, pxx, i;
    uint16_t n;
    for (cy = 0; cy < SCALE; ++cy) {
        for (cx = 0; cx < SCALE; ++cx) {
            i = 0;
            for (py = 0; py < 8; ++py) {
                const uint8_t *row = px + ((cy * 8 + py) / SCALE) * 8;
                for (pxx = 0; pxx < 8; ++pxx) charbuf[i++] = row[(cx * 8 + pxx) / SCALE];
            }
            n = (uint16_t)tile * TILE_CHARS + cy * SCALE + cx;
            dma_copy(charbuf, 64, 0, ((uint32_t)(CHAR_BASE + n)) << 6);
        }
    }
}

static void build_tiles(void)
{
    uint16_t t;
    for (t = 0; t < N_FIELD; ++t) make_tile((uint8_t)t, field_tiles[t]);
    for (t = 0; t < 16; ++t) make_tile((uint8_t)(TUNNEL_TILE0 + t), tunnel_tiles[t]);
    for (t = 0; t < 64; ++t) charbuf[t] = BLANK_PIXEL;
    {
        uint8_t cy, cx;
        for (cy = 0; cy < SCALE; ++cy)
            for (cx = 0; cx < SCALE; ++cx)
                dma_copy(charbuf, 64, 0, ((uint32_t)(CHAR_BASE + (uint16_t)BLANK_TILE * TILE_CHARS + cy * SCALE + cx)) << 6);
    }
}

/* which tile shows in screen character (sc, sr)? */
static uint16_t char_at(uint8_t sc, uint8_t sr)
{
    uint8_t lc, lr, cc, cr, sub;
    uint16_t tile;
    if (sc < FIELD_X0 || sc >= FIELD_X0 + FIELD_CH_COLS * SCALE ||
        sr < FIELD_Y0 || sr >= FIELD_Y0 + FIELD_CH_ROWS * SCALE)
        return CHAR_BASE + (uint16_t)BLANK_TILE * TILE_CHARS;
    lc = sc - FIELD_X0; lr = sr - FIELD_Y0;
    cc = lc / SCALE; cr = lr / SCALE;
    sub = (lr % SCALE) * SCALE + (lc % SCALE);
    tile = (uint16_t)cr * FIELD_CH_COLS + cc;              /* the dirt picture */
    if (cr >= 1 && cc >= 1 && cc <= COLS && map[cr][cc - 1] == 0)
        tile = TUNNEL_TILE0 + tunnel_mask(cc - 1, cr);      /* a dug cell: the tunnel tile for its neighbours */
    return CHAR_BASE + tile * TILE_CHARS + sub;
}

static void draw_screen(void)
{
    uint8_t sr, sc;
    for (sr = 0; sr < SCREEN_ROWS; ++sr) {
        for (sc = 0; sc < SCREEN_COLS; ++sc) {
            uint16_t ch = char_at(sc, sr);
            rowbuf[sc * 2] = (uint8_t)ch;
            rowbuf[sc * 2 + 1] = (uint8_t)(ch >> 8);
        }
        dma_copy(rowbuf, SCREEN_COLS * 2, 0, SCREEN_RAM + (uint32_t)sr * (SCREEN_COLS * 2));
    }
    /* colour RAM (two bytes per character): all zero = plain full-colour characters */
    dma_fill(0, SCREEN_COLS * SCREEN_ROWS * 2, 0xFF, 0x80000UL + COLOUR_OFS);
}

static void video_init(void)
{
    POKE(0xD02F, 0x47); POKE(0xD02F, 0x53);          /* unlock the VIC-IV registers */
    POKE(0xD030, PEEK(0xD030) | 0x04);               /* colours 0-15 come from the palette RAM too */
    POKE(0xD070, 0x54);                              /* palette bank 1 is mapped in, and used for text and sprites */
    set_palette();
    POKE(0xD020, BLANK_PIXEL); POKE(0xD021, BLANK_PIXEL);
    POKE(0xD031, PEEK(0xD031) | 0x88);               /* H640 + V400: 640x400 */
    POKE(0xD054, 0x05);                              /* CHR16 (13-bit character numbers) + full-colour for chars > $FF */
    POKE(0xD05E, SCREEN_COLS);                       /* characters per row */
    POKE(0xD058, SCREEN_COLS * 2); POKE(0xD059, 0);  /* bytes per row */
    POKE(0xD07B, SCREEN_ROWS);                       /* rows */
    POKE(0xD060, (uint8_t)SCREEN_RAM);
    POKE(0xD061, (uint8_t)(SCREEN_RAM >> 8));
    POKE(0xD062, (uint8_t)(SCREEN_RAM >> 16));
    POKE(0xD063, PEEK(0xD063) & 0xF0);
    POKE(0xD064, (uint8_t)COLOUR_OFS);
    POKE(0xD065, (uint8_t)(COLOUR_OFS >> 8));
}

int main(void)
{
    uint32_t wait;
    __asm__("sei");                                  /* the ROM's interrupt routine would fight over the screen */
    POKE(0x00, 65);                                  /* 40 MHz */
    build_test_level();
    video_init();
    build_tiles();
    draw_screen();
#ifdef TEST_EXIT
    for (wait = 0; wait < 600000UL; ++wait) { }
    POKE(0xD6CF, 0x42);                              /* Xemu in -testing mode exits when this is written */
#endif
    for (;;) { }
}
