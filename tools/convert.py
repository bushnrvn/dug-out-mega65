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
SY = 4                           # screen rows per game pixel: every tile becomes SY characters, each pixel row repeated SY times
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
    if n.startswith(('tunnels', 'font', 'logo', 'portrait')):
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
    soft.append((n, w, h, list(px)))

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
# both colour sets have the same shapes: keep one set as bits (a pixel is lit if it is not the background) and the two colours
FONT_FG = []
for st in (0, 1):
    cols = set(v for g in font[st * 41:(st + 1) * 41] for v in g) - {COL_INK, 0}
    assert len(cols) == 1, cols
    FONT_FG.append(cols.pop())
font_bits = []
for idx in range(41):
    a, b = font[idx], font[41 + idx]
    assert [v not in (0, COL_INK) for v in a] == [v not in (0, COL_INK) for v in b]
    bits = 0
    for k, v in enumerate(a):
        if v not in (0, COL_INK):
            bits |= 1 << k
    font_bits.append([bits & 255, (bits >> 8) & 255, (bits >> 16) & 255])

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



# ---- TILES: the field and tunnel tiles as the characters the VIC-IV shows, written to a file on the disk. Tile t is
# characters t*SY .. t*SY+SY-1; character k holds the tile's pixel rows (k*8 .. k*8+7)//SY, each row 8 pixels wide.
os.makedirs(os.path.join(ROOT, 'build', 'assets'), exist_ok=True)
tiles_bin = bytearray()
for tile in field + tunnels:
    for k in range(SY):
        for j in range(8):
            r = (k * 8 + j) // SY
            tiles_bin += bytes(tile[r * 8:r * 8 + 8])
# a PRG-style file: the first two bytes are a load address, which the KERNAL's LOAD drops (the game picks the destination)
open(os.path.join(ROOT, 'build', 'assets', 'TILES.BIN'), 'wb').write(b'\x00\x01' + bytes(tiles_bin))


# ---- TITLE: the title screen's picture (the field with the title tunnels, the two panels, the logo and the static text),
# stored the same way as TILES so the game can copy it straight into characters. Moving things (the chase, the blinking
# "PUSH START") are drawn over it by the game.
def title_plane():
    plane = [row[:] for row in pic]
    mp = [[1] * 16 for _ in range(14)]
    for c in range(14):
        mp[0][c] = 0

    def carve(c, r, w, h):
        for j in range(h):
            for i in range(w):
                mp[r + j][c + i] = 0
    carve(0, 9, 14, 1); carve(3, 3, 1, 7); carve(10, 5, 1, 5); carve(3, 5, 8, 1); carve(6, 11, 5, 1); carve(6, 9, 1, 3)

    def mask(c, r):
        m = 0
        if mp[r - 1][c]: m |= 1
        if mp[r][c + 1]: m |= 2
        if mp[r + 1][c]: m |= 4
        if c == 0 or mp[r][c - 1]: m |= 8
        return m
    for rr in range(1, 13):
        for cc in range(1, 15):
            if mp[rr][cc - 1] == 0:
                t = tunnels[mask(cc - 1, rr)]
                for j in range(8):
                    for i in range(8):
                        plane[rr * 8 + j][cc * 8 + i] = t[j * 8 + i]

    def box(x, y, w, h, v):
        for j in range(h):
            for i in range(w):
                plane[y + j][x + i] = v

    def panel(x, y, w, h):
        box(x, y, w, h, COL_INK)
        box(x + 2, y + 1, w - 4, 1, COL_RIM)
        box(x + 2, y + h - 2, w - 4, 1, COL_RIM)

    def put(img, iw, ih, x, y, transparent):
        for j in range(ih):
            for i in range(iw):
                v = img[j * iw + i]
                if v or not transparent:
                    plane[y + j][x + i] = v

    def text(x, y, msg, st):
        for ch in msg:
            if 'A' <= ch <= 'Z': idx = ord(ch) - 65
            elif '0' <= ch <= '9': idx = 26 + ord(ch) - 48
            else: idx = {'-': 36, ':': 37, '!': 38, '.': 39, "'": 40}.get(ch, -1)
            if idx >= 0:
                put(font[st * 41 + idx], 4, 4 + 2, x, y, False)
            x += 4

    panel(14, 4, 100, 50)
    lx, ly, lw, lh = slots['logo']
    logo = [spr_sheet[ly + j][lx + i] for j in range(lh) for i in range(lw)]
    put(logo, lw, lh, 64 - lw // 2, 11, True)
    panel(20, 84, 88, 12)
    msg = 'Z:THROW CURSORS:DIG'
    text(64 - len(msg) * 2, 87, msg, 0)
    return plane


def plane_to_chars(plane):
    out = bytearray()
    for rr in range(13):
        for cc in range(16):
            for k in range(SY):
                for j in range(8):
                    r = (k * 8 + j) // SY
                    out += bytes(plane[rr * 8 + r][cc * 8:cc * 8 + 8])
    return bytes(out)


plane = title_plane()
open(os.path.join(ROOT, 'build', 'assets', 'TITLE.BIN'), 'wb').write(b'\x00\x01' + plane_to_chars(plane))


def write_png(path, rows, scale=4):
    import zlib, struct
    h, w = len(rows), len(rows[0])
    raw = bytearray()
    for r in rows:
        line = bytearray()
        for v in r:
            line += bytes(pal[v]) * scale
        for _ in range(scale):
            raw += b'\x00' + line
    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w * scale, h * scale, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(bytes(raw))) + chunk(b'IEND', b''))


write_png(os.path.join(ROOT, 'build', 'assets', 'title_preview.png'), plane)


# ---- OVER and WIN: the game over and victory pictures. Each is the 128x128 art cropped to the 116 rows the display has
# (the bottom 12 rows are dirt, which the score plaque covers on the GameTank too), with the static parts baked in. File
# layout: the top 12 rows as 96 characters (the score strip's place on screen), then the other 104 rows as 13 x 16 tiles of
# 4 characters, like TILES.
ART_ROWS = 116


def img_box(img, x, y, w, h, v):
    for j in range(h):
        for i in range(w):
            img[y + j][x + i] = v


def img_text(img, x, y, msg, st):
    for ch in msg:
        if 'A' <= ch <= 'Z': idx = ord(ch) - 65
        elif '0' <= ch <= '9': idx = 26 + ord(ch) - 48
        else: idx = {'-': 36, ':': 37, '!': 38, '.': 39, "'": 40}.get(ch, -1)
        if idx >= 0:
            g = font[st * 41 + idx]
            for j in range(6):
                for i in range(4):
                    img[y + j][x + i] = g[j * 4 + i]
        x += 4


def img_text_center(img, y, msg, st):
    img_text(img, 64 - len(msg) * 2, y, msg, st)


def load_art(name):
    w, h, rows = read_bmp(os.path.join(ROOT, 'assets', name))
    assert (w, h) == (128, 128)
    return [r[:] for r in rows[:ART_ROWS]]


def score_plaque(img):                              # the box that holds RUNS and BEST (the numbers are drawn live)
    img_box(img, 1, 104, 126, 12, COL_INK)
    img_box(img, 3, 105, 122, 1, COL_RIM)


def art_to_file(img):
    out = bytearray()
    for cy in range(6):                             # the strip: 12 game rows as 6 character rows
        for cx in range(16):
            for j in range(8):
                out += bytes(img[(cy * 8 + j) // SY][cx * 8:cx * 8 + 8])
    out += plane_to_chars(img[12:])
    return b'\x00\x01' + bytes(out)


over = load_art('over.bmp')
img_box(over, 64 - 11 * 2 - 2, 29, 11 * 4 + 3, 9, COL_INK)     # a dark backing, so the art's dots do not run through the letters
img_text_center(over, 31, "YOU'RE OUT!", 1)
for y in range(39, 49):                                          # the STRIKE 3 plaque: its dot grid (colour 92) ran through the lettering
    for x in range(31, 98):
        if over[y][x] == 92: over[y][x] = COL_INK
score_plaque(over)
win = load_art('end.bmp')
img_box(win, 14, 34, 100, 22, COL_INK)
img_box(win, 15, 35, 98, 1, COL_RIM)
img_box(win, 15, 55, 98, 1, COL_RIM)
img_text_center(win, 37, 'NINE INNINGS COMPLETE', 1)
score_plaque(win)
open(os.path.join(ROOT, 'build', 'assets', 'OVER.BIN'), 'wb').write(art_to_file(over))
open(os.path.join(ROOT, 'build', 'assets', 'WIN.BIN'), 'wb').write(art_to_file(win))
write_png(os.path.join(ROOT, 'build', 'assets', 'over_preview.png'), over, 3)
write_png(os.path.join(ROOT, 'build', 'assets', 'win_preview.png'), win, 3)


# ---- INTRO: the background of the enemy introduction and the "meet the opposition" screens: the plain dirt field with a big
# panel on it (the text and the marching enemies are drawn over it by the game). Stored like TITLE.
def intro_plane():
    plane = [row[:] for row in pic]
    x, y, w, h = 8, 10, 112, 86
    for j in range(h):
        for i in range(w):
            plane[y + j][x + i] = COL_INK
    for i in range(w - 4):
        plane[y + 1][x + 2 + i] = COL_RIM
        plane[y + h - 2][x + 2 + i] = COL_RIM
    return plane


open(os.path.join(ROOT, 'build', 'assets', 'INTRO.BIN'), 'wb').write(b'\x00\x01' + plane_to_chars(intro_plane()))
write_png(os.path.join(ROOT, 'build', 'assets', 'intro_preview.png'), intro_plane())

with open(os.path.join(ROOT, 'src', 'data.h'), 'w') as f:
    f.write('/* generated by tools/convert.py - do not edit */\n#ifndef DATA_H\n#define DATA_H\n')
    f.write('#define FIELD_CH_COLS %d\n#define FIELD_CH_ROWS %d\n#define BLANK_PIXEL %d\n' % (CH_COLS, CH_ROWS, blank))
    f.write('#define N_FIELD_TILES %d\n#define N_TILE_TOTAL %d\n' % (len(field), len(field) + 16))
    f.write('extern const unsigned char palette_rgb[256][3];\n')
    f.write('#define COL_INK %d\n#define COL_RIM %d\n' % (COL_INK, COL_RIM))
    f.write('/* the font: 41 glyphs of 4x6 pixels, one bit per pixel (row by row, low bit first), in two colour sets */\nextern const unsigned char font_bits[41][3];\n#define FONT_FG0 %d\n#define FONT_FG1 %d\nextern const unsigned char life_px[48];\n' % (FONT_FG[0], FONT_FG[1]))
    f.write('#endif\n')
with open(os.path.join(ROOT, 'src', 'data.c'), 'w') as f:
    f.write('/* generated by tools/convert.py - do not edit */\n#include "data.h"\n\n')
    f.write('const unsigned char font_bits[41][3] = {\n')
    for g in font_bits:
        f.write('    { ' + ', '.join(str(v) for v in g) + ' },\n')
    f.write('};\n\nconst unsigned char life_px[48] = { ' + ', '.join(str(v) for v in life) + ' };\n\n')
    f.write('const unsigned char palette_rgb[256][3] = {\n')
    for r, g, b in pal:
        f.write('    { %d, %d, %d },\n' % (r, g, b))
    f.write('};\n')
def cname(n):
    return n.upper().replace('-', '_')


# ---- SPRITES: the software sprites' pixels (one byte each, 0 = transparent) and the font's glyph images (24 bytes each, in
# both colours), as a data file the game copies into chip RAM at $10000 (bank 1), where the drawing code reads them with
# 28-bit pointers. The structures in sprites.c hold each sprite's size and its offset into that block.
GLYPH_OFF = 0x1900                          # the glyph images' place in the block (the sprites come first)
sprite_block = bytearray()
sprite_off = []
for n, w, h, px in soft:
    sprite_off.append(len(sprite_block))
    sprite_block += bytes(px)
assert len(sprite_block) <= GLYPH_OFF, len(sprite_block)
sprite_block += bytes(GLYPH_OFF - len(sprite_block))
for st in (0, 1):
    for idx in range(41):
        col = FONT_FG[st]
        for k in range(24):
            sprite_block.append(col if font_bits[idx][k >> 3] & (1 << (k & 7)) else 0)
open(os.path.join(ROOT, 'build', 'assets', 'SPRITES.BIN'), 'wb').write(b'\x00\x01' + bytes(sprite_block))

with open(os.path.join(ROOT, 'src', 'sprites.h'), 'w') as f:
    f.write('/* generated by tools/convert.py - do not edit */\n#ifndef SPRITES_H\n#define SPRITES_H\n')
    f.write('/* a sprite is w x h bytes (a GameTank palette index each, 0 = transparent) at offset off in the block that the\n * SPRITES file puts in chip RAM at $10000 */\n')
    f.write('typedef struct { unsigned char w, h; unsigned int off; } SoftSprite;\n')
    f.write('#define SPRITE_BLOCK_SIZE %d\n#define GLYPH_OFF %d       /* glyph images: set * 41 + index, 24 bytes each */\n' % (len(sprite_block), GLYPH_OFF))
    for i, (n, w, h, px) in enumerate(soft):
        f.write('#define SPR_%s %d\n' % (cname(n), i))
    f.write('#define N_SOFT_SPRITES %d\nextern const SoftSprite soft_sprites[N_SOFT_SPRITES];\n#endif\n' % len(soft))
with open(os.path.join(ROOT, 'src', 'sprites.c'), 'w') as f:
    f.write('/* generated by tools/convert.py - do not edit */\n#include "sprites.h"\n\n')
    f.write('const SoftSprite soft_sprites[N_SOFT_SPRITES] = {\n')
    for (n, w, h, px), o in zip(soft, sprite_off):
        f.write('    { %d, %d, %d },   /* %s */\n' % (w, h, o, n))
    f.write('};\n')

print('wrote src/data.c, src/data.h, src/sprites.c, src/sprites.h: %d field chars, %d soft sprites, blank pixel %d' % (len(field), len(soft), blank))
