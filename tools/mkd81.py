#!/usr/bin/env python3
"""Make a Commodore 1581 (.d81) disk image holding PRG files.

  python3 tools/mkd81.py out.d81 "DISK NAME" NAME=path [NAME=path ...]

The image has 80 tracks of 40 sectors. Track 40 holds the header (sector 0), the two block availability maps (sectors 1
and 2) and the directory (sector 3 onwards). Files are stored from track 1 upwards. Each data sector holds 254 bytes of
file data after a two-byte link to the next sector. The files are plain PRG entries; the first two bytes of each one are
its load address, which the loader ignores for the data files.
"""
import sys

TRACKS, SECS = 80, 40
HDR_TRACK = 40


def off(t, s):
    return ((t - 1) * SECS + s) * 256


def petscii(name, n=16):
    b = name.upper().encode('ascii')[:n]
    return b + b'\xa0' * (n - len(b))


def build(out, disk_name, files):
    img = bytearray(TRACKS * SECS * 256)
    free = {t: [True] * SECS for t in range(1, TRACKS + 1)}
    for s in range(4):                       # header, two BAM sectors and the first directory sector are in use
        free[HDR_TRACK][s] = False

    def alloc(prefer_track):
        # next free sector, scanning from track 1 upward and skipping the directory track
        for t in range(1, TRACKS + 1):
            if t == HDR_TRACK:
                continue
            for s in range(SECS):
                if free[t][s]:
                    free[t][s] = False
                    return t, s
        raise SystemExit('disk full')

    entries = []
    for name, path in files:
        data = open(path, 'rb').read()
        chunks = [data[i:i + 254] for i in range(0, len(data), 254)] or [b'']
        secs = [alloc(1) for _ in chunks]
        for i, c in enumerate(chunks):
            o = off(*secs[i])
            if i + 1 < len(chunks):
                img[o], img[o + 1] = secs[i + 1]
            else:
                img[o], img[o + 1] = 0, len(c) + 1
            img[o + 2:o + 2 + len(c)] = c
        entries.append((name, secs[0], len(chunks)))

    # directory sectors: 40/3, 40/4, ...
    dir_secs = [3]
    for n in range(0, len(entries), 8):
        if n // 8 >= len(dir_secs):
            s = dir_secs[-1] + 1
            free[HDR_TRACK][s] = False
            dir_secs.append(s)
    for di, s in enumerate(dir_secs):
        o = off(HDR_TRACK, s)
        if di + 1 < len(dir_secs):
            img[o], img[o + 1] = HDR_TRACK, dir_secs[di + 1]
        else:
            img[o], img[o + 1] = 0, 0xFF
        for k in range(8):
            idx = di * 8 + k
            if idx >= len(entries):
                break
            name, (t, sc), nblocks = entries[idx]
            e = o + k * 32
            img[e + 2] = 0x82                # PRG, closed
            img[e + 3], img[e + 4] = t, sc
            img[e + 5:e + 21] = petscii(name)
            img[e + 30], img[e + 31] = nblocks & 255, nblocks >> 8

    # header sector
    o = off(HDR_TRACK, 0)
    img[o], img[o + 1] = HDR_TRACK, 3
    img[o + 2], img[o + 3] = 0x44, 0xBB
    img[o + 4:o + 20] = petscii(disk_name)
    img[o + 20:o + 22] = b'\xa0\xa0'
    img[o + 22:o + 24] = b'DO'
    img[o + 24] = 0xA0
    img[o + 25], img[o + 26] = 0x33, 0x44
    img[o + 27:o + 29] = b'\xa0\xa0'

    # BAM: per track one count byte and five bytes of flags, bit set = free
    for half, bam_sec in ((0, 1), (1, 2)):
        o = off(HDR_TRACK, bam_sec)
        img[o], img[o + 1] = (HDR_TRACK, 2) if half == 0 else (0, 0xFF)
        img[o + 2], img[o + 3] = 0x44, 0xBB
        img[o + 4:o + 6] = b'DO'
        img[o + 6] = 0xC0
        for i in range(40):
            t = half * 40 + i + 1
            flags = 0
            for s in range(SECS):
                if free[t][s]:
                    flags |= 1 << s
            cnt = sum(free[t])
            p = o + 16 + i * 6
            img[p] = cnt
            for b in range(5):
                img[p + 1 + b] = (flags >> (8 * b)) & 255
    open(out, 'wb').write(img)


if __name__ == '__main__':
    out, name = sys.argv[1], sys.argv[2]
    files = [tuple(a.split('=', 1)) for a in sys.argv[3:]]
    build(out, name, files)
    print('wrote %s: %d files' % (out, len(files)))
