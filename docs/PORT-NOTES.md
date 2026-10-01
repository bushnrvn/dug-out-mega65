# Port notes

How the GameTank version maps onto the MEGA65. Nothing here is built yet.

| Part | GameTank | MEGA65 |
|---|---|---|
| CPU | 65C02, 3.5 MHz, 8 KB RAM, banked 2 MB cartridge | 45GS02, about 40 MHz, flat memory, no banking to manage |
| Screen | 128x128, blitter and draw queue | VIC-IV tile map and sprites |
| Dirt and tunnels | patched sprite-RAM page, redrawn per dug cell | full-colour 8x8 tile map, update the cell |
| Doug, enemies, boss | blitted sprites | hardware sprites (boss is one 16x16 sprite) |
| Sound | 4 FM voices, MIDI converted to a tick stream | SID chips: music converted to a SID player format |
| Input | gamepad | joystick port and keyboard |
| High score | flash sector on the cartridge | file on the SD card |

## What carries over
The rules: map and dirt logic, enemy behaviour, scoring, innings and levels. Mostly portable C.

## What has to be written
Drawing, input, timing, sound, saving, and an asset converter from the GameTank sprite sheets and palette.

## Open decisions
- Keep the 128x128 look, scaled up, or redraw at a higher resolution.
- How to package it (a disk image on itch.io is the usual way).

## Measured in Xemu (Open ROM banner shows a MEGA65 R3, PAL)
- Display window: x 80..720, y 104..504 (640x400). The text area starts at (80, 104) by default; it is moved to (144, 148)
  to centre the 512x312 picture.
- CHRXSCL ($D05A): character width is about 980/value pixels, rounded down. 30 gives exactly 32 px, i.e. 4x.
- CHRYSCL ($D05B) had no effect in this mode at any value I tried, so vertical scaling is done in the tile data instead
  (each tile is four characters stacked, with every pixel row repeated 4x).
- Sprites: full-colour, 16 sprite pixels wide = 32 physical pixels; with the native-vertical flag each row is one raster.
  Screen x = 2*X + 31, screen y = Y (both need their top bit in $D010 / $D077). In full-colour mode the low nybble of
  the sprite's colour register ($D027...) is the transparent pixel value and must be 0.
- Sprite palette: bank 2, entry = sprite number * 16 + pixel value. Characters use palette bank 1 unchanged from the
  GameTank palette.
- Test builds (`-DTEST_EXIT`) end by writing $42 to $D6CF, which makes Xemu (-testing) exit and save a screenshot.

## Memory layout
The PRG loads at $2001 in C65 mode. Below $8000 is plain RAM, but ROMs are laid over $8000-$BFFF; `src/early.s` runs first (as a
constructor, from a segment below $8000) and clears ROM8/ROMA/ROMC in $D030, after which $2001-$CFFF are all usable
(`cfg/dugout.cfg`, HIMEM $D000). Checked in Xemu by running a function placed above $8000.

## Software sprites
Only eight hardware sprites exist and the game can have about twenty things moving, so apart from Doug everything is drawn by the CPU into
copies of the characters it covers (`src/render.c`): each frame the plain map is copied, every cell an actor touches gets its own copy of its tile,
the actors are drawn into the copies, then the copies and the map are uploaded to the buffer that is not being shown and the display switches
to it. There are two screen buffers and two pools of 80 copies. Characters are 8 wide and a game pixel is one character pixel across and four
down (the tile data repeats each row 4x); Doug is drawn like the others.

## Memory (measured)
The game is a small program plus data files on the disk image (`build/dugout.d81`), the way the GameTank keeps its art in the cartridge.
- Start-up (`src/early.s`, before the ROMs are switched off, because it uses the KERNAL): LOAD each data file from device 8 into bank 5
  (`SETBNK`), then copy it with DMA into attic RAM ($8000000, 64K per file; file n goes to attic bank n). The first two bytes of a data
  file are a dummy load address: the KERNAL's LOAD drops them. File names are written in lower case in the source because both the
  assembler and the C compiler turn text into PETSCII, where lower case is the disk's upper case.
- $2001-$CFFF: program, data and BSS (the ROMs overlaid on $8000-$CFFF are switched off at start-up). $D000-$FFFF is under the I/O and
  the KERNAL ROM, which stays mapped (the sound interrupt and the best score save both use it).
- Chip RAM (the VIC-IV shows characters from anywhere up to $5FFFF):
  - **Do not write to the first 8K of bank 1 ($10000-$11FFF).** The C65 system keeps its disk state there. Overwriting it makes every
    KERNAL disk call fail with "device not present" (found by bisecting the start-up: this is what broke saving the best score).
  - $12100, $12900: the two screen maps. $13100: the score strip's characters. $14900: the pools of sprite-copy characters (2 x 160).
    $1A000: the sound data. $1E000: the banner characters.
  - bank 4: the tile characters ($40000-$4DFFF), then the software sprites' pixels at $4E000.
  - bank 5: the title / game over / victory picture's characters from $50000 (up to 928 characters), then the font's glyph images at $5E800.
- Attic RAM: the data files.

## The best score
Kept in a small file, `hiscore`, on the disk (5 bytes: two marker bytes, the hundreds, the tens digit). It is loaded at start-up with
the other files and saved at the end of a run that set a new best, with the KERNAL's SAVE. Two things about saving at run time:
- The KERNAL must see the machine the way it was at start-up: interrupts off, the ROMs overlaid on $8000-$CFFF again, and the CIA
  interrupts left alone. The save routine (low memory, `src/early.s`) sets that up and puts everything back afterwards.
- The KERNAL's disk code reprograms CIA 1's timer, which the frame pacing reads. The game restarts the timer after a save, and the
  pacing loop is bounded so a stopped timer can never freeze the game (it did, before: the music kept playing and the picture froze).
- Xemu writes the file into the mounted .d81, so the best score survives between runs there too.

## Sound
`src/sound.s`: a 60 Hz player on the VIC raster interrupt, reached through the KERNAL's interrupt entry (it pushes A, X, Y, Z and B and
jumps through the RAM vector at $0314; the player's handler replaces the KERNAL's own and leaves the same way). Melody, bass and snare on
SID 1, harmony on SID 2, sound effects on SID 3. All voices of a song are padded to the same length so they loop together. Pitch is
calculated for the NTSC clock (the game runs the MEGA65 at 60 Hz); I could not hear it, so the tuning is untested by ear.
