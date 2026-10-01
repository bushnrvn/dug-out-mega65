/* Hardware check, built in only with -DHW_DIAG (make diag). Page 1 is shown before the game starts: what the disk loader put in
 * attic RAM and whether the memory areas the game uses read back. Then the game draws its title screen for a few seconds, and
 * page 2 reports what the video chip, the screen and the sound chip look like. Photograph the three screens. */
#include <stdint.h>
#include "platform.h"
#include "hwdiag.h"

#ifdef HW_DIAG_PRE
static uint8_t buf[256];
#else
static uint8_t buf[16];
#endif

/* tiny direct-to-screen text (conio and printf would not fit in memory): 80 columns, screen RAM at $0800 */
static uint16_t tpos;
static void tclear(void)
{
    uint16_t i;
    for (i = 0; i < 2000; ++i) { POKE(0x0800 + i, 32); POKE(0xD800 + i, 1); }
    tpos = 0;
}
static void tc(char c)
{
    if (c == '\n') { tpos = (tpos / 80 + 1) * 80; return; }
    if (c >= 'a' && c <= 'z') c = c - 'a' + 1;
    if (tpos < 2000) POKE(0x0800 + tpos, (uint8_t)c);
    ++tpos;
}
static void ts(const char *p) { while (*p) tc(*p++); }
static void th(uint8_t v)
{
    uint8_t d = v >> 4, e = v & 15;
    tc(d < 10 ? '0' + d : 'a' + d - 10 - 0); tc(e < 10 ? '0' + e : 'a' + e - 10);
}
static void th16(uint16_t v) { th(v >> 8); th((uint8_t)v); }

#ifdef HW_DIAG_PRE
static uint16_t sum_attic(uint32_t base, uint8_t *first)
{
    uint16_t s = 0, i; uint8_t b;
    for (b = 0; b < 16; ++b) {
        dma_copy28(0x80, base + ((uint32_t)b << 8), 0, (uint32_t)(uint16_t)buf, 256);
        if (b == 0) for (i = 0; i < 4; ++i) first[i] = buf[i];
        for (i = 0; i < 256; ++i) s += buf[i];
    }
    return s;
}

static uint8_t test_ram(uint8_t mb, uint32_t addr, uint8_t val)
{
    uint16_t i; uint8_t ok = 1;
    dma_job(0x03, 256, val, mb, addr);
    dma_copy28(mb, addr, 0, (uint32_t)(uint16_t)buf, 256);
    for (i = 0; i < 256; ++i) if (buf[i] != val) ok = 0;
    return ok;
}

#endif

static void peek_mem(uint8_t mb, uint32_t addr, uint8_t n)
{
    uint8_t i;
    dma_copy28(mb, addr, 0, (uint32_t)(uint16_t)buf, 16);
    for (i = 0; i < n; ++i) th(buf[i]);
    tc(' ');
}

static void kv(const char *k, uint8_t v) { ts(k); tc(' '); th(v); tc(' '); }

#ifdef HW_DIAG_PRE
static void hold(void)
{
#ifndef HW_DIAG_STOP
    uint16_t n;
    ts("\nwait for the next screen");
    for (n = 0; n < 900; ++n) wait_frame();
#endif
}

void hw_diag_pre(void)
{
    uint8_t f[4], g[4], n, t1, t2, t3, t4, t5, t6;
    uint16_t s, m;
    tclear();
    ts("dug out hw check 1\n");
    for (n = 0; n < 7; ++n) {
        s = sum_attic((uint32_t)n << 16, f);
        m = sum_attic(((uint32_t)n << 16) + 0x4000, g);
        ts("file "); th(n); ts(" sum "); th16(s); tc(' '); th16(m); ts("  ");
        th(f[0]); tc(' '); th(f[1]); tc(' '); th(f[2]); tc(' '); th(f[3]); tc('\n');
    }
    t1 = test_ram(0, 0x40000UL, 0xA5); t2 = test_ram(0, 0x50000UL, 0x5A); t3 = test_ram(0, 0x5E800UL, 0x3C);
    ts("ram ok 40000 "); th(t1); ts(" 50000 "); th(t2); ts(" 5e800 "); th(t3); tc('\n');
    t4 = test_ram(0x80, 0xF0000UL, 0x5A); t5 = test_ram(0xFF, 0x82000UL, 0x00); t6 = test_ram(0xFF, 0x82000UL, 0x2C);
    ts("attic "); th(t4); ts(" cram "); th(t5); tc(' '); th(t6); tc('\n');
    kv("d030", PEEK(0xD030)); kv("d031", PEEK(0xD031)); kv("d054", PEEK(0xD054)); kv("d06f", PEEK(0xD06F)); tc('\n');
    kv("d704", PEEK(0xD704)); kv("cpu", PEEK(0x00)); tc('\n');
    hold();
#ifdef HW_DIAG_STOP
#if HW_DIAG_STOP == 1
    POKE(0xD6CF, 0x42);
#endif
#endif
}

#endif

static const uint16_t regs[] = { 0xD030, 0xD031, 0xD054, 0xD058, 0xD059, 0xD05A, 0xD05C, 0xD05D, 0xD05E, 0xD060, 0xD061, 0xD062, 0xD063,
    0xD064, 0xD065, 0xD06F, 0xD070, 0xD07B, 0xD04C, 0xD04D, 0xD04E, 0xD04F, 0xD048, 0xD049, 0xD04A, 0xD04B, 0xD020, 0xD021,
    0x0314, 0x0315, 0xD41B, 0xD41B };
static const uint32_t areas[] = { 0x12100UL, 0x12900UL, 0x13100UL, 0x40000UL + 64UL * 3, 0x40000UL + 64UL * 40, 0x50000UL,
    0x50000UL + 64UL * 200, 0x4E000UL, 0x5E800UL, 0x14900UL, 0x14900UL + 64UL * 20 };

void hw_diag_post(void)
{
    uint8_t r[32], i;
    uint16_t n;
    for (n = 0; n < 300; ++n) wait_frame();          /* the game's own picture, to look at */
    for (i = 0; i < 32; ++i) r[i] = PEEK(regs[i]);
    /* back to a plain text screen (the video chip's registers are still unlocked from the game's set-up) */
    POKE(0xD070, 0x00); POKE(0xD030, 0x00); POKE(0xD05D, 0x00); POKE(0xD04C, 0x50); POKE(0xD04D, 0x00);
    POKE(0xD048, 0x38); POKE(0xD049, 0x00); POKE(0xD04A, 0x08); POKE(0xD04B, 0x02);
    POKE(0xD031, 0x00); POKE(0xD054, 0x00); POKE(0xD05A, 0x78); POKE(0xD05C, 0x00);
    POKE(0xD060, 0x00); POKE(0xD061, 0x08); POKE(0xD062, 0x00);
    POKE(0xD064, 0x00); POKE(0xD065, 0x00); POKE(0xD058, 80); POKE(0xD059, 0); POKE(0xD05E, 80);
    POKE(0xD020, 0); POKE(0xD021, 6); POKE(0xD07B, 24); POKE(0xD018, 0x14);
    tclear();
    ts("dug out hw check 2\n");
    for (i = 0; i < 32; ++i) {                       /* register address and value, four to a row */
        th16(regs[i]); tc('='); th(r[i]); ts("  ");
        if ((i & 3) == 3) tc('\n');
    }
    for (i = 0; i < 11; ++i) {                       /* the first bytes of each memory area the picture is made of */
        th((uint8_t)(areas[i] >> 16)); th16((uint16_t)areas[i]); tc(' ');
        peek_mem(0, areas[i], 8); tc('\n');
    }
    ts("colour "); peek_mem(0xFF, 0x82000UL, 8); peek_mem(0xFF, 0x82000UL + 0x100, 8); tc('\n');
#ifdef HW_DIAG_STOP
    POKE(0xD6CF, 0x42);
#endif
    for (;;) { }
}
