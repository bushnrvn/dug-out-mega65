# Dug Out for the MEGA65

A port of Dug Out, a baseball-themed digging arcade game, to the MEGA65. The original is a GameTank game:
https://github.com/bushnrvn/dug-out

You are Doug, a ballplayer who digs under the ballpark. Tunnel through the dirt, strike the creatures out with
your fastball, and drop home plates on the rest. Nine innings, then a boss.

## Status

Milestone 3 works in the Xemu emulator:
- the real level generator runs: random pockets, shafts and boulders for each inning, seeded per game;
- the game logic is the GameTank version's own C (map, Doug's movement and digging, enemy behaviour, strikes, rocks,
  scoring), compiled unchanged into `src/game.c`; only drawing, input, timing and sound are MEGA65 code;
- the dirt, tunnels, boulders, enemies and effects are all drawn;
- Doug is a hardware sprite, everything else is drawn by the CPU into the field's characters ("software sprites"), which
  avoids the VIC-IV's limit of eight sprites.

Not done yet: the score and lives display, the title, intro and end screens, sound and music, the high score.
The keyboard and joystick code has not been exercised (the automated checks cannot press keys). Everything has been run
only in the emulator. See `docs/PORT-NOTES.md`.

`tools/convert.py` turns the art in `assets/` into `src/data.c` and `src/sprites.c` (run by `make`).

## Build

```sh
export CC65_HOME=/path/to/cc65      # a cc65 build with the mega65 target
make                                # writes build/dugout.prg
```

Run `build/dugout.prg` on a MEGA65, or in the Xemu emulator:

```sh
scripts/run-xemu.sh                 # opens the emulator
SHOT=/tmp/out.png scripts/run-xemu.sh   # runs, then saves a screenshot once the program returns to BASIC
```

Set `XEMU` to your `xmega65` binary and `M65_ROM` to a MEGA65 or C65 ROM image you own. The ROM is not in this repo.

## Credits

Game, art and design: bushnrvn. Built with cc65. Music from public domain scores: Grieg's Hall of the Mountain
King and Take Me Out to the Ball Game.
