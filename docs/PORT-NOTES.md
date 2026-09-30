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
- CHRYSCL ($D05B) had no effect in this mode at any value I tried, so vertical 3x is done in the tile data instead
  (each tile is three characters stacked, with every pixel row repeated 3x).
- Sprites: full-colour, 16 sprite pixels wide = 32 physical pixels; with the native-vertical flag each row is one raster.
  Screen x = 2*X + 31, screen y = Y (both need their top bit in $D010 / $D077). In full-colour mode the low nybble of
  the sprite's colour register ($D027...) is the transparent pixel value and must be 0.
- Sprite palette: bank 2, entry = sprite number * 16 + pixel value. Characters use palette bank 1 unchanged from the
  GameTank palette.
- Test builds (`-DTEST_EXIT`) end by writing $42 to $D6CF, which makes Xemu (-testing) exit and save a screenshot.

## Memory layout
The PRG loads at $2001 in C65 mode. Below $8000 is plain RAM, but ROMs are laid over $8000-$BFFF; `src/early.s` runs first (as a
constructor, from a segment below $8000) and clears ROM8/ROMA/ROMC in $D030, after which $2001-$BFFF are all usable
(`cfg/dugout.cfg`, HIMEM $C000). Checked in Xemu by running a function placed above $8000. The large tables are only ever
read by DMA, which ignores the ROM overlay anyway.
