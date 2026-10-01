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
calculated for the NTSC clock (the game runs the MEGA65 at 60 Hz); it has been heard on an R6 and sounds right.

## Real hardware (tested on an R6) versus Xemu
Xemu hid all of these. The game was first built and tested only in Xemu, and none of it worked on the R6 until they were fixed.
- **HOTREG ($D05D bit 7).** While it is on (the default), touching VIC-II registers (including $D031) makes the chip recompute the
  screen layout from the old VIC-II registers, a moment later, wiping chars per row ($D05E), rows ($D07B), the text area start
  ($D04C/$D04E), the borders ($D048-$D04B) and so on. Xemu does this instantly, so the game's settings always won there. The game now
  switches HOTREG off before it sets anything. That also stops the chip setting $D05B (character line height) itself, so the game sets
  $D05B = 0 (one raster per line in 400-line mode); left at the 200-line value 1, everything is drawn twice as tall.
- **The 60 Hz switch ($D06F bit 7)** resets the same registers a few frames later on hardware (Xemu does too). The game sets its video
  registers again until they have stayed put twice in a row (about a second at start-up).
- **Raster numbering.** After the switch to 60 Hz the first raster line is 7 ($D06F reads back $87), so a raster interrupt on line 0 never
  fires. The sound interrupt uses line 32.
- **Input.** Releasing the keyboard column lines and reading the joystick straight away reads garbage at 40 MHz: the line driven last has
  not risen yet and "up" read as held. The game reads the joystick first, and waits after each column change. Diagonals (an 8-way
  stick, or two cursor keys) are resolved to one direction, or the player turns on the spot every tick.
- **Disk.** Mounting the .d81 over the network (m65 connect) once delivered bad data (a missing music channel and no sound effect on one
  run; fine on the next). The loader only checks that each file loaded, not that the bytes are right. The SD card is the safe way.
- Tools for finding such things are in `tools/hwdiag/` and `src/hwdiag.c` (`make diag`): they show register values and memory on
  screen so a photo from the real machine can be compared with Xemu.
- **Picture shape.** The MEGA65's 720x480 output is shown as 4:3, so its pixels are about 11% narrower than square, and a 5:4 monitor
  squeezes it a little more. The characters are drawn 15% wider (`XSCL` 26, 4.6 screen pixels per game pixel, `FIELD_X` 104 to keep the
  picture centred) so that the picture is square on a 4:3 display and on a 5:4 monitor. Xemu shows the raw 3:2 frame, so in Xemu's window
  the picture looks wider than it is on a real display.
- **Scanlines and other user settings.** The game sets only the layout registers listed above and never writes a scanline or video-filter
  setting. It uses the 400-line mode, where every line carries picture data.

## Gameplay changes made together with the GameTank version (unreleased there)
- The Heater's flame hurts only where it is drawn, and one fireball (not two) circles each Heater, with its hit box matching.
- A stunned enemy kills Doug on touch (the Groundskeeper still does not). Bats leave their pocket after 100 ticks instead of 150.
- Extra life every 10,000 points (`next_life_h`, in hundreds, was 300).
- Game over screen: a dark backing behind "YOU'RE OUT!" and the dot grid cleared from the "STRIKE 3" plaque's lettering (tools/convert.py).
- A gold bar (new 8x8 sprite `gold`) in the shared sprite sheet; `SP_GOLD_X/Y` in gen_art.h) lies in a random cell near the middle of a random
  enemy cave each inning: 500 points when Doug touches it, enemies walk over it. The sprite block grew, so `GLYPH_OFF` is now $1A00.
- The theme's drum track now comes in after about 3 seconds instead of 30 (`drums_in_sooner` in tools/make_sound.py adds the same hit
  on the same beat grid earlier), because innings are shorter than 30 seconds of music.
- When two enemies are left (`enemies_left <= 2`), every Vumpire, Heater and bat gives up the chase and runs for the top (`e_flee`).
  Walkers keep to the tunnels: `flee_dir` is a breadth-first search over open cells to any cell of row 0, and if there is no way (a
  sealed cave) they carry on as before. They no longer spit fire or start a revival ritual while fleeing. Bats fly straight up through
  the dirt, as they do everything else. One that reaches the top costs the score a kill would have paid at the depth it started from
  (`e_pts`, hundreds; the score floors at 0) and counts as gone, so the inning can end. A hit still stuns it, three strikes still put it
  out. Groundskeepers and Mad Scott never flee. The popup shows the loss with a minus sign (bit 15 of `pop_v`). While more than two
  are left they head for Doug a little more directly than before (fewer random turns in `choose_dir`).
  The search needs 448 bytes, so `DYN_MAX` (sprite copy slots) went from 160 to 152; the most ever seen in use is 134.
- The Groundskeeper digs as it moves (`enemy_update`, the same code as Mad Scott's smashing, quietly): it heads for the nearest open cell that is
  not joined to its own (`region_mark`, a flood fill over open cells, using the same scratch arrays as the fleeing search), so it works its way
  from one sealed cave to the next, leaving a tunnel. Its cells are marked `paid[] = 0x10`; `refill_cell` now only rakes cells that Doug was paid
  for (`paid` 1-4), so it never closes a cave or its own tunnel. In a 900-tick test at inning 5 it joined five caves in a chain.
- Head bob: the player and the walking enemies (Vumpires, Groundskeepers, Mad Scott; not Heaters or bats) drop their heads a pixel on the beat
  while the theme plays (`bob`, `draw_bob`). The beat is `beat_tick` in game.c: the theme's snare hits fall on a grid 52.15 frames (26.075
  game ticks) apart, the first 26.9 frames in, and the song (8371 frames) loops, so the counter restarts with it. `beat_start` is called when
  the theme starts. It assumes ticks of exactly two frames; a slow frame would drift the nod a little until the song loops.
  DYN_MAX went down to 140 to make room for the code.
- Saving the best score lays the ROMs over $8000-$CFFF, which hides the sound player's variables (they live up there). If the raster interrupt came
  in during the save, the player read ROM bytes as its sound-effect pointer and Xemu stopped on "Unhandled memory read ... $607C06" at game over.
  The save routine now sets `save_busy` (in the low, always-visible ONCE segment) and the interrupt does nothing while it is set.
- The head bob is twice as fast (a nod on each snare hit and on the beat between: `beat_tick` compares `beat_acc` with both halves of the period) and
  only while a sprite is moving: `pmv` for Doug and `e_mv[]` for the enemies are held for a few ticks after each step, because digging and the
  slower enemies move only every other tick and the nod would flicker otherwise.
