#!/usr/bin/env python3
"""Write a PNG preview of an 8-bit indexed BMP using the game's palette: bmp_preview.py in.bmp out.png [scale]"""
import json, os, struct, sys, zlib
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
pal = json.load(open(os.path.join(ROOT, 'assets', 'palette.json')))
d = open(sys.argv[1], 'rb').read()
off = int.from_bytes(d[10:14], 'little'); w = int.from_bytes(d[18:22], 'little'); h = int.from_bytes(d[22:26], 'little', signed=True)
rows = [list(d[off + (abs(h) - 1 - y if h > 0 else y) * w:][:w]) for y in range(abs(h))]
sc = int(sys.argv[3]) if len(sys.argv) > 3 else 3
raw = bytearray()
for r in rows:
    line = bytearray()
    for v in r: line += bytes(pal[v]) * sc
    for _ in range(sc): raw += b'\x00' + line
def chunk(t, x):
    c = struct.pack('>I', len(x)) + t + x
    return c + struct.pack('>I', zlib.crc32(t + x) & 0xFFFFFFFF)
open(sys.argv[2], 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w * sc, abs(h) * sc, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(bytes(raw))) + chunk(b'IEND', b''))
print(sys.argv[1], w, abs(h))
