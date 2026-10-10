# Dug Out for the MEGA65

A port of Dug Out, a baseball-themed digging arcade game, to the MEGA65. The original is a GameTank game:
https://github.com/bushnrvn/dug-out

You are Doug, a ballplayer who digs under the ballpark. Tunnel through the dirt, strike the creatures out with
your fastball, and drop home plates on the rest. 


## Controls
**W A S D** move and dig, **Space** throws, **Return** starts and pauses. The cursor keys and a joystick in port 2 also work (a joystick's fire button throws).
Under Xemu use W A S D rather than the cursor keys: Xemu gives Left and Right one shared key of the keyboard matrix (Left is Right plus a pretend Shift, and Up is Down plus the same Shift),
so quick overlapping presses of the cursor keys can make it drop a key you are still holding (see `xemu/c64_kbd_mapping.c` and `emutools_hid.c` in the Xemu source).

## Quitting
Hold RUN/STOP for about a second. If the game was started from the MEGA65 Desktop it returns to the desktop; if it was started any other way nothing happens (reset the machine).

## Credits

Game design: bushnrvn. Built with cc65. Music from public domain scores: Grieg's Hall of the Mountain
King and Take Me Out to the Ball Game.
