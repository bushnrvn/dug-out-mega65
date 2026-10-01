/* Memory copies (the DMA controller), timing and input. */
#include "platform.h"

static uint8_t dmalist[17];

void dma_job_src(uint8_t cmd, uint16_t count, uint16_t src, uint8_t src_bank, uint8_t dst_mb, uint32_t dst)
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
    dmalist[9] = src_bank;                 /* source bank */
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

/* chip RAM character <-> normal memory, 64 bytes, using only 16-bit arithmetic (characters start at $40000 + 64 * number
 * minus the base: here `ch` is the absolute character number, so its address is ch * 64) */
static void dma_char(uint8_t to_char, uint16_t ch, uint8_t *mem)
{
    uint16_t lo = (ch & 0x03FF) << 6;                 /* address within its 64K bank */
    uint8_t bank = (uint8_t)(ch >> 10);
    dmalist[0] = 0x0B; dmalist[1] = 0x81; dmalist[2] = 0x00; dmalist[3] = 0x00;
    dmalist[4] = 0x00;
    dmalist[5] = 64; dmalist[6] = 0;
    if (to_char) {
        dmalist[7] = (uint8_t)(uint16_t)mem; dmalist[8] = (uint8_t)((uint16_t)mem >> 8); dmalist[9] = 0;
        dmalist[10] = (uint8_t)lo; dmalist[11] = (uint8_t)(lo >> 8); dmalist[12] = bank;
    } else {
        dmalist[7] = (uint8_t)lo; dmalist[8] = (uint8_t)(lo >> 8); dmalist[9] = bank;
        dmalist[10] = (uint8_t)(uint16_t)mem; dmalist[11] = (uint8_t)((uint16_t)mem >> 8); dmalist[12] = 0;
    }
    dmalist[13] = 0; dmalist[14] = 0; dmalist[15] = 0;
    POKE(0xD702, 0x00);
    POKE(0xD701, (uint16_t)dmalist >> 8);
    POKE(0xD705, (uint8_t)(uint16_t)dmalist);
}

void dma_char_in(uint16_t ch, uint8_t *dst) { dma_char(0, ch, dst); }          /* character -> memory */
void dma_char_out(const uint8_t *src, uint16_t ch) { dma_char(1, ch, (uint8_t *)src); }   /* memory -> character */

/* copy anywhere in the 28-bit space: source and destination are a megabyte number and a 20-bit offset each */
void dma_copy28(uint8_t src_mb, uint32_t src, uint8_t dst_mb, uint32_t dst, uint16_t count)
{
    dmalist[0] = 0x0B;                     /* 12-byte list format */
    dmalist[1] = 0x80; dmalist[2] = src_mb;
    dmalist[3] = 0x81; dmalist[4] = dst_mb;
    dmalist[5] = 0x00;                     /* end of options */
    dmalist[6] = 0x00;                     /* copy */
    dmalist[7] = (uint8_t)count; dmalist[8] = (uint8_t)(count >> 8);
    dmalist[9] = (uint8_t)src; dmalist[10] = (uint8_t)(src >> 8); dmalist[11] = (uint8_t)((src >> 16) & 0x0F);
    dmalist[12] = (uint8_t)dst; dmalist[13] = (uint8_t)(dst >> 8); dmalist[14] = (uint8_t)((dst >> 16) & 0x0F);
    dmalist[15] = 0x00;
    POKE(0xD702, 0x00);
    POKE(0xD701, (uint16_t)dmalist >> 8);
    POKE(0xD705, (uint8_t)(uint16_t)dmalist);
}

void dma_job(uint8_t cmd, uint16_t count, uint16_t src, uint8_t dst_mb, uint32_t dst)
{
    dma_job_src(cmd, count, src, 0, dst_mb, dst);
}

uint16_t frames_seen;

void wait_frame(void)
{
    ++frames_seen;
    while (!(PEEK(0xD011) & 0x80)) { }                /* raster is in the lower half of the frame (line 256 or more) ... */
    while (PEEK(0xD011) & 0x80) { }                   /* ... and the next frame starts when it wraps to the top */
}

static uint8_t key_down(uint8_t col, uint8_t row)
{
    POKE(0xDC00, (uint8_t)~(1u << col));
    return (PEEK(0xDC01) & (1u << row)) == 0;
}

uint8_t read_input(void)
{
    uint8_t r = 0, shift, j;
    POKE(0xDC02, 0xFF);                               /* port A drives the keyboard columns */
    POKE(0xDC03, 0x00);
    shift = key_down(1, 7) | key_down(6, 4);
    if (key_down(0, 7)) r |= shift ? INPUT_MASK_UP : INPUT_MASK_DOWN;       /* cursor down / up */
    if (key_down(0, 2)) r |= shift ? INPUT_MASK_LEFT : INPUT_MASK_RIGHT;    /* cursor right / left */
    if (key_down(1, 4)) r |= INPUT_MASK_A;                                  /* Z: throw */
    if (key_down(0, 1)) r |= INPUT_MASK_START;                              /* Return */
    POKE(0xDC00, 0xFF);
    POKE(0xDC02, 0x00);                               /* port A back to input: joystick port 2 */
    j = (uint8_t)~PEEK(0xDC00);
    if (j & 0x01) r |= INPUT_MASK_UP;
    if (j & 0x02) r |= INPUT_MASK_DOWN;
    if (j & 0x04) r |= INPUT_MASK_LEFT;
    if (j & 0x08) r |= INPUT_MASK_RIGHT;
    if (j & 0x10) r |= INPUT_MASK_A;
    return r;
}

uint16_t timer_now(void)       /* CIA 1 timer A: counts down about a million times a second */
{
    return PEEK(0xDC04) | ((uint16_t)PEEK(0xDC05) << 8);
}

void timer_start(void)
{
    POKE(0xDC04, 0xFF); POKE(0xDC05, 0xFF);
    POKE(0xDC0E, (PEEK(0xDC0E) & 0xC0) | 0x11);        /* continuous, force load, start */
}
