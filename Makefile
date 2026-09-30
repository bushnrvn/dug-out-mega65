# Dug Out for the MEGA65. Needs cc65 with the mega65 target (set CC65_HOME if it is not on your path).
CL65 ?= cl65
SRCS := src/main.c src/data.c
OUT  := build/dugout.prg
TEST := build/dugout-test.prg

all: $(OUT)

$(OUT): $(SRCS) src/data.h
	@mkdir -p build
	$(CL65) -t mega65 -O -o $@ $(SRCS)

# a build that exits the emulator once the screen is drawn (used with SHOT=... scripts/run-xemu.sh)
$(TEST): $(SRCS) src/data.h
	@mkdir -p build
	$(CL65) -t mega65 -O -DTEST_EXIT -o $@ $(SRCS)

src/data.c src/data.h: tools/convert.py assets/bg.bmp assets/gen_art.h assets/palette.json
	python3 tools/convert.py

clean:
	rm -rf build

.PHONY: all clean
