#!/usr/bin/env python3
"""
Turns the game's art into data for the MEGA65 build.

  python3 tools/convert.py        writes src/data.c and src/data.h

Field: the baked dirt picture (assets/bg.bmp, rows 0-103) is cut into 8x8 characters. The game's cells start at x=7,
so the picture is shifted one pixel right so every cell lines up with a character. That gives a 16x13 grid of
characters: character column c+1 is game cell column c.
Tunnels: the 16 neighbour-mask tiles from assets/gen_art.h.
The palette is the GameTank palette, used unchanged, so pixel values are palette indices.
"""
import json, os, re, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')


def read_bmp(path):
    d = open(path, 'rb').read()
    off = int.from_bytes(d[10:14], 'little')
    w = int.from_bytes(d[18:22], 'little')
    h = int.from_bytes(d[22:26], 'little', signed=True)
    assert int.from_bytes(d[28:30], 'little') == 8, 'expected an 8-bit indexed BMP'
    rows = []
    for y in range(abs(h)):
        src = abs(h) - 1 - y if h > 0 else y
        rows.append(list(d[off + src * w: off + src * w + w]))
    return w, abs(h), rows


w, h, bg = read_bmp(os.path.join(ROOT, 'assets', 'bg.bmp'))
assert (w, h) == (128, 128)

# 128x104 picture, shifted right by one pixel
edge_l = [bg[y][0] for y in range(104)]
edge_r = [bg[y][125] for y in range(104)]
pic = []
for y in range(104):
    row = [edge_l[y]] + bg[y][0:126] + [edge_r[y]]
    pic.append(row)
assert len(pic[0]) == 128

CH_COLS, CH_ROWS = 16, 13
N_BIG = 156                      # how many field characters fit in $D000-$F6FF (156 * 64 = 9984 bytes); the system uses $F700 and up
field = []
for cr in range(CH_ROWS):
    for cc in range(CH_COLS):
        ch = []
        for j in range(8):
            ch += pic[cr * 8 + j][cc * 8: cc * 8 + 8]
        field.append(ch)

art = open(os.path.join(ROOT, 'assets', 'gen_art.h')).read()
m = re.search(r'tunnel_px\[16\]\[64\]\s*=\s*\{(.*?)\n\};', art, re.S)
nums = [int(x) for x in re.findall(r'\d+', m.group(1))]
assert len(nums) == 16 * 64, len(nums)
tunnels = [nums[i * 64:(i + 1) * 64] for i in range(16)]

# the colour that fills the page margins: the most common pixel in the picture's left column
from collections import Counter
blank = Counter(edge_l).most_common(1)[0][0]

pal = json.load(open(os.path.join(ROOT, 'assets', 'palette.json')))

# ---- sprites: full-colour (4 bits per pixel), 16 pixels wide. Each game pixel becomes 2 sprite pixels across and
# 3 sprite rows down, which at the game's 4x3 screen scale is exactly one game pixel per 4x3 physical pixels.
SPR_ROWS = 24
sw, sh, spr_sheet = read_bmp(os.path.join(ROOT, 'assets', 'spr.bmp'))
slots = json.load(open(os.path.join(ROOT, 'assets', 'slots.json')))['spr']


def sprite_pixels(name):
    x, y, w, h = slots[name]
    return [spr_sheet[y + j][x: x + w] for j in range(h)]


def pack_sprite(rows, nib):
    out = []
    for y in range(SPR_ROWS):
        src = rows[y // 3]
        px = [nib.get(src[x // 2], 0) if src[x // 2] else 0 for x in range(16)]
        for i in range(0, 16, 2):
            out.append((px[i] << 4) | px[i + 1])
    return out


def make_actor(names):
    frames = [sprite_pixels(n) for n in names]
    cols = []
    for f in frames:
        for row in f:
            for v in row:
                if v and v not in cols:
                    cols.append(v)
    assert len(cols) <= 15, (names[0], len(cols))
    nib = {c: i + 1 for i, c in enumerate(cols)}
    data = [pack_sprite(f, nib) for f in frames]
    palette = [(0, 0, 0)] + [tuple(pal[c]) for c in cols] + [(0, 0, 0)] * (15 - len(cols))
    return data, palette


# ---- software sprites: everything except Doug (a hardware sprite) is drawn into the character graphics by the CPU
SOFT = []
for n in sorted(slots):
    if n.startswith(('doug_', 'tunnels', 'font', 'logo', 'portrait')):
        continue
    SOFT.append(('spr', n))
sw2, sh2, spr2_sheet = read_bmp(os.path.join(ROOT, 'assets', 'spr2.bmp'))
slots2 = json.load(open(os.path.join(ROOT, 'assets', 'slots.json')))['spr2']
for n in sorted(slots2):
    SOFT.append(('spr2', n))


def soft_pixels(sheet_name, name):
    sheet, sl = (spr_sheet, slots) if sheet_name == 'spr' else (spr2_sheet, slots2)
    x, y, w, h = sl[name]
    return w, h, [v for j in range(h) for v in sheet[y + j][x: x + w]]


soft = []
for sh, n in SOFT:
    w, h, px = soft_pixels(sh, n)
    cols = []
    for v in px:
        if v and v not in cols:
            cols.append(v)
    assert len(cols) <= 15, (n, len(cols))
    nib = {c: i + 1 for i, c in enumerate(cols)}
    packed = []
    for i in range(0, len(px), 2):
        a = nib.get(px[i], 0) if px[i] else 0
        b = nib.get(px[i + 1], 0) if i + 1 < len(px) and px[i + 1] else 0
        packed.append((a << 4) | b)
    soft.append((n, w, h, packed, [0] + cols + [0] * (15 - len(cols))))

# ---- the HUD font: 4x6 glyphs (two colour sets of 41), plus the 8x6 life icon, one byte per pixel (0 = clear)
def art_const(name):
    return int(re.search(r'#define %s (\d+)' % name, art).group(1))


FONT_X, FONT_Y = art_const('SP_FONT_X'), art_const('SP_FONT_Y')
LIFE_X, LIFE_Y = art_const('SP_LIFE_X'), art_const('SP_LIFE_Y')
COL_INK, COL_RIM = art_const('COL_INK'), art_const('COL_RIM')
font = []
for cell in range(82):
    gx, gy = FONT_X + ((cell & 31) << 2), FONT_Y + (cell >> 5) * 6
    font.append([spr_sheet[gy + j][gx + i] for j in range(6) for i in range(4)])
life = [spr_sheet[LIFE_Y + j][LIFE_X + i] for j in range(6) for i in range(8)]

doug_names = ['doug_%d_%d' % (d, f) for d in range(4) for f in range(2)] + ['doug_x0', 'doug_x1']
doug_data, doug_pal = make_actor(doug_names)

assert len(pal) == 256


def carr(name, rows, per_line=16):
    out = ['const unsigned char %s[%d][64] = {' % (name, len(rows))]
    for r in rows:
        out.append('    {')
        for i in range(0, 64, per_line):
            out.append('        ' + ', '.join(str(v) for v in r[i:i + per_line]) + ',')
        out.append('    },')
    out.append('};')
    return '\n'.join(out)


with open(os.path.join(ROOT, 'src', 'data.h'), 'w') as f:
    f.write('/* generated by tools/convert.py - do not edit */\n#ifndef DATA_H\n#define DATA_H\n')
    f.write('#define FIELD_CH_COLS %d\n#define FIELD_CH_ROWS %d\n#define BLANK_PIXEL %d\n' % (CH_COLS, CH_ROWS, blank))
    f.write('/* the first N_BIG field characters sit in the DMA-only area above $D000, the rest in normal memory */\n#define N_BIG %d\n' % N_BIG)
    f.write('extern const unsigned char field_tiles_a[%d][64];\nextern const unsigned char field_tiles_b[%d][64];\nextern const unsigned char tunnel_tiles[16][64];\n' % (N_BIG, len(field) - N_BIG))
    f.write('extern const unsigned char palette_rgb[256][3];\n')
    f.write('#define COL_INK %d\n#define COL_RIM %d\n' % (COL_INK, COL_RIM))
    f.write('/* HUD font: glyph = set * 41 + index, 4x6 pixels each; the life icon is 8x6 */\nextern const unsigned char font_px[82][24];\nextern const unsigned char life_px[48];\n')
    f.write('#define SPRITE_BYTES %d\n#define DOUG_FRAMES %d\n' % (SPR_ROWS * 8, len(doug_data)))
    f.write('extern const unsigned char doug_frames[%d][%d];\nextern const unsigned char doug_pal[16][3];\n#endif\n' % (len(doug_data), SPR_ROWS * 8))
with open(os.path.join(ROOT, 'src', 'data.c'), 'w') as f:
    f.write('/* generated by tools/convert.py - do not edit */\n#include "data.h"\n\n')
    f.write('/* the big tables live above $C000 (see cfg/dugout.cfg) and are only read by DMA */\n#pragma rodata-name (push, "BIGDATA")\n')
    f.write(carr('field_tiles_a', field[:N_BIG]) + '\n#pragma rodata-name (pop)\n\n')
    f.write(carr('field_tiles_b', field[N_BIG:]) + '\n\n' + carr('tunnel_tiles', tunnels) + '\n\n')
    f.write('const unsigned char doug_frames[%d][%d] = {\n' % (len(doug_data), SPR_ROWS * 8))
    for fr in doug_data:
        f.write('    { ' + ', '.join(str(v) for v in fr) + ' },\n')
    f.write('};\n\nconst unsigned char doug_pal[16][3] = {\n')
    for r, g, b in doug_pal:
        f.write('    { %d, %d, %d },\n' % (r, g, b))
    f.write('};\n\n')
    f.write('const unsigned char font_px[82][24] = {\n')
    for g in font:
        f.write('    { ' + ', '.join(str(v) for v in g) + ' },\n')
    f.write('};\n\nconst unsigned char life_px[48] = { ' + ', '.join(str(v) for v in life) + ' };\n\n')
    f.write('const unsigned char palette_rgb[256][3] = {\n')
    for r, g, b in pal:
        f.write('    { %d, %d, %d },\n' % (r, g, b))
    f.write('};\n')
def cname(n):
    return n.upper().replace('-', '_')


with open(os.path.join(ROOT, 'src', 'sprites.h'), 'w') as f:
    f.write('/* generated by tools/convert.py - do not edit */\n#ifndef SPRITES_H\n#define SPRITES_H\n')
    f.write('/* pixels are 4 bits each (high nybble first, 0 = transparent); pal maps a nybble to a GameTank palette index */\n')
    f.write('typedef struct { unsigned char w, h; const unsigned char *px; const unsigned char *pal; } SoftSprite;\n')
    for i, (n, w, h, px, pal_) in enumerate(soft):
        f.write('#define SPR_%s %d\n' % (cname(n), i))
    f.write('#define N_SOFT_SPRITES %d\nextern const SoftSprite soft_sprites[N_SOFT_SPRITES];\n#endif\n' % len(soft))
with open(os.path.join(ROOT, 'src', 'sprites.c'), 'w') as f:
    f.write('/* generated by tools/convert.py - do not edit */\n#include "sprites.h"\n\n')
    for n, w, h, px, pal_ in soft:
        f.write('static const unsigned char px_%s[%d] = { %s };\n' % (cname(n), len(px), ', '.join(str(v) for v in px)))
        f.write('static const unsigned char pal_%s[16] = { %s };\n' % (cname(n), ', '.join(str(v) for v in pal_)))
    f.write('\nconst SoftSprite soft_sprites[N_SOFT_SPRITES] = {\n')
    for n, w, h, px, pal_ in soft:
        f.write('    { %d, %d, px_%s, pal_%s },\n' % (w, h, cname(n), cname(n)))
    f.write('};\n')

print('wrote src/data.c, src/data.h, src/sprites.c, src/sprites.h: %d field chars, %d soft sprites, blank pixel %d' % (len(field), len(soft), blank))
