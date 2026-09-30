# Dug Out for the MEGA65. Needs cc65 with the mega65 target (set CC65_HOME if it is not on your path).
CL65 ?= cl65
OUT  := build/dugout.prg

$(OUT): src/main.c
	@mkdir -p build
	$(CL65) -t mega65 -O -o $@ src/main.c

clean:
	rm -rf build

.PHONY: clean
