# Dug Out for the MEGA65

A port of Dug Out, a baseball-themed digging arcade game, to the MEGA65. The original is a GameTank game:
https://github.com/bushnrvn/dug-out

You are Doug, a ballplayer who digs under the ballpark. Tunnel through the dirt, strike the creatures out with
your fastball, and drop home plates on the rest. Nine innings, then a boss.

## Status

Just started. The repo builds a placeholder program; the game itself is not ported yet. See `docs/PORT-NOTES.md`.

## Build

```sh
export CC65_HOME=/path/to/cc65      # a cc65 build with the mega65 target
make                                # writes build/dugout.prg
```

Run `build/dugout.prg` on a MEGA65 or in the Xemu emulator.

## Credits

Game, art and design: bushnrvn. Built with cc65. Music from public domain scores: Grieg's Hall of the Mountain
King and Take Me Out to the Ball Game.
