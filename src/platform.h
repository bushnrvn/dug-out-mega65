#ifndef PLATFORM_H
#define PLATFORM_H
#include <stdint.h>
#include "game.h"     /* INPUT_MASK_* */

#define POKE(a, v) (*(volatile uint8_t *)(a) = (uint8_t)(v))
#define PEEK(a)    (*(volatile uint8_t *)(a))

/* One enhanced DMA job: copy (cmd 0) or fill (cmd 3). The source is in the first 64K of memory (or the fill value); the
 * destination is a 28-bit address: MB (bits 27-20) and a 20-bit offset. */
void dma_job(uint8_t cmd, uint16_t count, uint16_t src, uint8_t dst_mb, uint32_t dst);
#define dma_copy(src, count, dst_mb, dst) dma_job(0x00, (count), (uint16_t)(src), (dst_mb), (dst))
#define dma_fill(val, count, dst_mb, dst) dma_job(0x03, (count), (val), (dst_mb), (dst))

uint8_t read_input(void);    /* INPUT_MASK_* bits from the keyboard and joystick port 2 */
uint16_t timer_now(void);    /* free-running, counts down */
void timer_start(void);
void wait_frame(void);       /* returns at the start of the next video frame */
#endif
