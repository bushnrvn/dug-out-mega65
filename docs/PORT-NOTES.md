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
