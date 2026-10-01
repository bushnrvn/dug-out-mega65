#!/bin/sh
# Run build/dugout.prg in the Xemu MEGA65 emulator.
#   XEMU    path to the xmega65 binary (default: ~/Desktop/xemu-master/build/apps/xmega65.app/Contents/MacOS/xmega65)
#   M65_ROM path to a MEGA65/C65 ROM image you own (default: ~/Desktop/920413.bin). The ROM is not part of this repo.
# The disk image build/dugout.d81 holds the game and its data files (make builds it). With SHOT=/path/out.png it runs
# the test build (build/dugout-test.prg, made with -DTEST_EXIT), which tells Xemu to exit once the screen is drawn, and
# saves a screenshot on the way out.
cd "$(dirname "$0")/.."
XEMU="${XEMU:-$HOME/Desktop/xemu-master/build/apps/xmega65.app/Contents/MacOS/xmega65}"
M65_ROM="${M65_ROM:-$HOME/Desktop/920413.bin}"
if [ -n "$SHOT" ]; then
    exec "$XEMU" -besure -fastboot -sleepless -testing -rom "$M65_ROM" -8 build/dugout.d81 -prg build/dugout-test.prg -screenshot "$SHOT"
fi
exec "$XEMU" -besure -fastboot -rom "$M65_ROM" -8 build/dugout.d81 -prg build/dugout.prg
