# Dug Out for the MEGA65

A port of Dug Out, a baseball-themed digging arcade game, to the MEGA65. The original is a GameTank game:
https://github.com/bushnrvn/dug-out

You are Doug, a ballplayer who digs under the ballpark. Tunnel through the dirt, strike the creatures out with
your fastball, and drop home plates on the rest. Nine innings, then a boss.

## Status

A full round plays in the Xemu emulator, from the title screen to the game over or victory screen:
- the real level generator runs: random pockets, shafts and boulders for each inning, seeded per game;
- the game logic is the GameTank version's own C (map, Doug's movement and digging, enemy behaviour, strikes, rocks,
  scoring), compiled unchanged into `src/game.c`; only drawing, input, timing and sound are MEGA65 code;
- everything is drawn by the CPU into the field's characters ("software sprites"), which avoids the VIC-IV's limit of
  eight sprites; a game pixel is 4x4 screen pixels, at 60 Hz, 30 game ticks a second like the GameTank version;
- the score and lives strip, the title screen, the game over and victory screens, with the GameTank's own artwork.

Also: music and sound effects on the SID chips, the best score saved on the disk, and the GameTank's READY, PAUSED and INNING OVER banners and timings.

The enemy introduction screens (before innings 1, 2, 3, 5 and 9), the "meet the opposition" screen when the title sits idle, the floating score popups and the depth marker on the gauges are in too.

The keyboard and joystick code has not been
exercised (the automated checks cannot press keys). Everything has been run only in the emulator, none of it on real
hardware. See `docs/PORT-NOTES.md`.

`tools/convert.py` turns the art in `assets/` into `src/data.c`, `src/sprites.c` and the disk's data files in `build/assets/` (run by `make`); `tools/mkd81.py` writes the disk image.

## Build

```sh
export CC65_HOME=/path/to/cc65      # a cc65 build with the mega65 target
make                                # writes build/dugout.d81: the game and its data files
```

The game loads its art from the disk when it starts, so it needs the whole `build/dugout.d81` (mount it on a MEGA65 and run `DUGOUT`), or run it in the Xemu emulator:

```sh
scripts/run-xemu.sh                 # opens the emulator
SHOT=/tmp/out.png scripts/run-xemu.sh   # runs the test build (make with -DTEST_EXIT), then saves a screenshot
```

Set `XEMU` to your `xmega65` binary and `M65_ROM` to a MEGA65 or C65 ROM image you own. The ROM is not in this repo.

## Credits

Game design: bushnrvn. Built with cc65. Music from public domain scores: Grieg's Hall of the Mountain
King and Take Me Out to the Ball Game.
