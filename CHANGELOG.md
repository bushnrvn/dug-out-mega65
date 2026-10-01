# Changelog

Versions follow MAJOR.MINOR.PATCH, and this port's version tracks the GameTank game's (the game rules are shared). The current version is in
`VERSION`. Each release is a git tag (`v1.4.0`) with the game disk (`dugout.d81`) attached.

## 1.4.0
First public version of the MEGA65 port. It plays the whole game of Dug Out 1.4.0 and has been run on a real MEGA65 (an R6) as well as in Xemu.

* Runs from a disk image: the game, its art and its music load from `dugout.d81` into attic RAM when it starts.
* Title, intro, attract, game over and victory screens, the READY / PAUSED / INNING OVER banners, score popups and the depth marker.
* Four-voice music and sound effects on the SID chips, and a best score that is saved to the disk and shown on the title screen.
* Keyboard (cursor keys, Z to throw, Return) and joystick port 2.
* Real hardware needed several things Xemu hides (the video chip's HOTREG setting, the raster line after the 60 Hz switch, joystick line timing, the
  sound interrupt during a save); they are explained in `docs/PORT-NOTES.md`.
* The picture is drawn about 15% wider than square, because a monitor shows the MEGA65's 720x480 signal as 4:3.
* The game of 1.4.0: enemies that hunt Doug along the open tunnels and, with two left, run for the top (costing their value if they get out);
  a Groundskeeper that digs from cave to cave; a gold bar worth 500 points in a random cave each inning; Heaters with one fireball whose flame
  hurts only where it is drawn; stunned enemies that kill on touch; faster bats; an extra life every 10,000 points; and heads that nod on the beat.
