/* Memory copies (the DMA controller), timing and input. */
#include "platform.h"

static uint8_t dmalist[16];

void dma_job(uint8_t cmd, uint16_t count, uint16_t src, uint8_t dst_mb, uint32_t dst)
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

void wait_frame(void)
{
    while (PEEK(0xD012) != 0xFF) { }
    while (PEEK(0xD012) == 0xFF) { }
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
