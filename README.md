# Dug Out for the MEGA65

A port of Dug Out, a baseball-themed digging arcade game, to the MEGA65. The original is a GameTank game:
https://github.com/bushnrvn/dug-out

You are Doug, a ballplayer who digs under the ballpark. Tunnel through the dirt, strike the creatures out with
your fastball, and drop home plates on the rest. Nine innings, then a boss.

## Status

Milestone 1 works in the Xemu emulator: the dirt field and auto-tiled tunnels are drawn on the VIC-IV at 3x from the
game's own art, using a hard-coded test level. No Doug, enemies, sound or input yet. See `docs/PORT-NOTES.md`.

`tools/convert.py` turns the art in `assets/` into `src/data.c` (run by `make`).

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
