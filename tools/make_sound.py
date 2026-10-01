#!/usr/bin/env python3
"""
Turns the game's songs (assets/audio/*.mid) and sound effects (assets/audio/*.sfx) into build/assets/SOUND.BIN, the data
the MEGA65 sound player (src/sound.s) reads from chip RAM at $1A000.

SOUND.BIN (after its 2-byte load address), offsets from the start of the data:
    0      128 x 2   SID frequency register value for each MIDI note number (low byte, high byte)
    256    N_SONGS x 8   per song, four 16-bit offsets, one per voice (0 = the voice is not used)
    ...    N_SFX x 2     per sound effect, a 16-bit offset
    ...    the streams
A music stream is a list of events: note (1-127, or 0 for a rest), then the length in frames (16 bits, low byte first); the
list ends with $FF. A sound effect is a frame count followed by 4 bytes per frame (60 per second): frequency low, frequency
high, SID control byte (waveform and gate), volume 0-15.
"""
import os, struct, sys, math

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
AUDIO = os.path.join(ROOT, 'assets', 'audio')
SID_CLOCK = 1022727.0          # NTSC; the game runs the MEGA65 in 60 Hz mode. (PAL would be 985248.)
FPS = 60.0

SONGS = ['title', 'theme', 'clear', 'over']
SFX = ['dig', 'shoot', 'pump1', 'pump2', 'pump3', 'pop', 'fall', 'thud', 'die', 'flame', 'squash', 'ready', 'oneup', 'start']
WAVE = {'dig': 0x80, 'shoot': 0x40, 'pump1': 0x10, 'pump2': 0x10, 'pump3': 0x10, 'pop': 0x80, 'fall': 0x20, 'thud': 0x80,
        'die': 0x40, 'flame': 0x80, 'squash': 0x80, 'ready': 0x40, 'oneup': 0x40, 'start': 0x40}
N_VOICES = 4
DATA_SIZE = 16384


def sid_freq(note):
    f = 440.0 * 2 ** ((note - 69) / 12.0)
    return min(65535, int(round(f * 16777216.0 / SID_CLOCK)))


def read_vlq(d, i):
    v = 0
    while True:
        b = d[i]; i += 1
        v = (v << 7) | (b & 0x7F)
        if not b & 0x80:
            return v, i


def parse_midi(path):
    d = open(path, 'rb').read()
    assert d[:4] == b'MThd'
    ppq = struct.unpack('>H', d[12:14])[0]
    assert d[14:18] == b'MTrk'
    n = struct.unpack('>I', d[18:22])[0]
    i, end = 22, 22 + n
    t, status, tempo = 0, 0, 500000
    notes = {c: [] for c in range(N_VOICES)}       # channel -> [(start, end, note)]
    on = {}
    while i < end:
        dt, i = read_vlq(d, i)
        t += dt
        b = d[i]
        if b == 0xFF:
            typ = d[i + 1]; ln, j = read_vlq(d, i + 2)
            if typ == 0x51:
                tempo = int.from_bytes(d[j:j + 3], 'big')
            i = j + ln
            continue
        if b & 0x80:
            status = b; i += 1
        kind, ch = status & 0xF0, status & 0x0F
        if kind in (0x90, 0x80):
            note, vel = d[i], d[i + 1]; i += 2
            if kind == 0x90 and vel > 0:
                if ch in on:                       # a new note ends the one still sounding
                    s, nn = on.pop(ch)
                    notes[ch].append((s, t, nn))
                on[ch] = (t, note)
            elif ch in on and on[ch][1] == note:
                s, nn = on.pop(ch)
                notes[ch].append((s, t, nn))
        else:
            i += 2 if kind in (0xA0, 0xB0, 0xE0) else 1
    frames = lambda tick: int(round(tick * tempo / ppq / 1e6 * FPS))
    # every voice is padded with a rest to the end of the song, so they all loop together (voices of different lengths
    # would drift apart a little more each time round)
    total = max((frames(e) for ch in range(N_VOICES) for _, e, _ in notes[ch]), default=0)
    streams = []
    for ch in range(N_VOICES):
        ev, pos = bytearray(), 0
        for s, e, nn in sorted(notes[ch]):
            fs, fe = frames(s), frames(e)
            if fs > pos:
                ev += struct.pack('<BH', 0, fs - pos)
            dur = max(1, fe - fs)
            ev += struct.pack('<BH', nn, dur)
            pos = fs + dur
        if ev and pos < total:
            ev += struct.pack('<BH', 0, total - pos)
        streams.append(bytes(ev) + b'\xff' if ev else b'')
    return streams


def parse_sfx(name):
    d = open(os.path.join(AUDIO, name + '.sfx'), 'rb').read()
    n = d[0]
    out = bytearray([n])
    wave = WAVE[name]
    for k in range(n):
        f = d[2 + k * 8: 2 + k * 8 + 8]
        amp, note = f[3], f[7]
        fr = sid_freq(note)
        out += bytes([fr & 255, fr >> 8, (wave | 1) if amp else wave, min(15, amp)])
    return bytes(out)


def main():
    out_path = os.path.join(ROOT, 'build', 'assets', 'SOUND.BIN')
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    freq = b''.join(struct.pack('<H', sid_freq(n)) for n in range(128))
    song_table_size = len(SONGS) * N_VOICES * 2
    sfx_table_size = len(SFX) * 2
    pos = 256 + song_table_size + sfx_table_size
    blobs, song_offs, sfx_offs = bytearray(), [], []
    for s in SONGS:
        offs = []
        for stream in parse_midi(os.path.join(AUDIO, s + '.mid')):
            if stream:
                offs.append(pos + len(blobs)); blobs += stream
            else:
                offs.append(0)
        song_offs.append(offs)
    for s in SFX:
        sfx_offs.append(pos + len(blobs)); blobs += parse_sfx(s)
    data = bytearray(freq)
    for offs in song_offs:
        for o in offs:
            data += struct.pack('<H', o)
    for o in sfx_offs:
        data += struct.pack('<H', o)
    data += blobs
    assert len(data) <= DATA_SIZE, len(data)
    data += bytes(DATA_SIZE - len(data))
    open(out_path, 'wb').write(b'\x00\x01' + bytes(data))
    print('wrote %s: %d bytes of sound data used (of %d)' % (out_path, len(blobs) + pos, DATA_SIZE))


if __name__ == '__main__':
    main()
