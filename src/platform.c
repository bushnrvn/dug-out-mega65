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

/* Real hardware needs time: at 40 MHz a line that has just been driven or released has not yet settled when it is read back. */
static void settle(uint8_t n)
{
    while (n--) __asm__("nop");
}

static uint8_t key_down(uint8_t col, uint8_t row)
{
    POKE(0xDC00, (uint8_t)~(1u << col));
    settle(24);
    return (PEEK(0xDC01) & (1u << row)) == 0;
}

/* One direction at a time, and it is the key pressed last: pressing a second key takes over at once, and when that is let go the first (if still held) carries on.
 * (Opposite keys do not cancel each other: mashing Left and Right always turns Doug the way of the newest press.) */
static uint8_t cur_dir, prev_dirs;
#define DIR_BITS (INPUT_MASK_UP | INPUT_MASK_DOWN | INPUT_MASK_LEFT | INPUT_MASK_RIGHT)

static uint8_t one_direction(uint8_t d)
{
    uint8_t fresh = d & ~prev_dirs;                               /* pressed since the last call */
    prev_dirs = d;
    if (fresh) cur_dir = fresh & (uint8_t)-fresh;                 /* the new key (if two at once, either) */
    else if (!(cur_dir & d)) cur_dir = d & (uint8_t)-d;           /* the one we were following was let go: take whichever is still held */
    return cur_dir;
}

uint8_t quit_requested;                                /* RUN/STOP has been held for about a second (see read_input) */
static uint8_t stop_ticks;

static uint8_t pf;                                /* the cursor left / up flags of the last call */

uint8_t read_input(void)
{
    uint8_t r = 0, f, j;
    POKE(0xDC02, 0x00);                               /* the joystick (port 2) first, while the lines are still as the last call left them: inputs */
    POKE(0xDC00, 0xFF);
    settle(60);
    j = (uint8_t)~PEEK(0xDC00);
    if (j & 0x01) r |= INPUT_MASK_UP;
    if (j & 0x02) r |= INPUT_MASK_DOWN;
    if (j & 0x04) r |= INPUT_MASK_LEFT;
    if (j & 0x08) r |= INPUT_MASK_RIGHT;
    if (j & 0x10) r |= INPUT_MASK_A;
    POKE(0xDC02, 0xFF);                               /* then port A drives the keyboard columns */
    POKE(0xDC03, 0x00);
    /* The cursor keys. On the Commodore keyboard Left and Up are Right and Down with Shift, which cannot tell "Up and Right" from "Left and Down" when
     * two are held, and the Shift can drop a moment before the key does. The MEGA65 has the two keys' own flags ($D60F bit 0: cursor left, bit 1: cursor up),
     * so Left and Up come from those, and the matrix's Right and Down only count when the flag for the same key is clear (a Left press also shows as Right
     * in the matrix) and was clear a moment ago too (the matrix and the flag can change a tick apart). */
    f = PEEK(0xD60F) & 3;
    if (f & 1) r |= INPUT_MASK_LEFT;
    if (f & 2) r |= INPUT_MASK_UP;
    if (key_down(0, 7) && !((f | pf) & 2)) r |= INPUT_MASK_DOWN;
    if (key_down(0, 2) && !((f | pf) & 1)) r |= INPUT_MASK_RIGHT;
    pf = f;
    if (key_down(1, 1)) r |= INPUT_MASK_UP;           /* W A S D, the main controls: ordinary keys that cannot interfere with each other (Xemu, for one, gives the cursor keys */
    if (key_down(1, 2)) r |= INPUT_MASK_LEFT;         /* one shared matrix position and a pretend Shift, so overlapping presses of them can cancel */
    if (key_down(1, 5)) r |= INPUT_MASK_DOWN;
    if (key_down(2, 2)) r |= INPUT_MASK_RIGHT;
    if (key_down(7, 4)) r |= INPUT_MASK_A;                                  /* Space: throw */
    if (key_down(0, 1)) r |= INPUT_MASK_START;                              /* Return */
    if (key_down(7, 7)) { if (stop_ticks < 255) ++stop_ticks; } else stop_ticks = 0;     /* RUN/STOP: held for 30 ticks (about a second) quits */
    quit_requested = stop_ticks >= 30;
    POKE(0xDC00, 0xFF);
    POKE(0xDC02, 0x00);                               /* port A back to input, for the next call's joystick read */
    return (r & ~DIR_BITS) | one_direction(r & DIR_BITS);
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
