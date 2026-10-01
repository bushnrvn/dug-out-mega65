# Dug Out for the MEGA65. Needs cc65 with the mega65 target (set CC65_HOME if it is not on your path) and Python 3.
CL65 ?= cl65
SRCS := src/early.s src/blit.s src/sound.s src/main.c src/platform.c src/render.c src/game.c src/data.c src/sprites.c
CFG  := cfg/dugout.cfg
OUT  := build/dugout.prg
DATA := build/assets/TILES.BIN build/assets/TITLE.BIN build/assets/OVER.BIN build/assets/WIN.BIN build/assets/SOUND.BIN
D81  := build/dugout.d81
DATAD81 := build/data.d81

all: $(D81)

$(OUT): $(SRCS) src/data.h src/sprites.h $(CFG)
	@mkdir -p build
	$(CL65) -t mega65 -C $(CFG) -O -o $@ $(SRCS)

# the disk image: the game and its data files (the game loads the data into attic RAM when it starts)
$(D81): $(OUT) $(DATA) tools/mkd81.py
	python3 tools/mkd81.py $@ "DUG OUT" DUGOUT=$(OUT) TILES=build/assets/TILES.BIN TITLE=build/assets/TITLE.BIN OVER=build/assets/OVER.BIN WIN=build/assets/WIN.BIN SOUND=build/assets/SOUND.BIN

# a disk with only the data files, for test builds that are loaded straight into Xemu
$(DATAD81): $(DATA) tools/mkd81.py
	python3 tools/mkd81.py $@ "DUG OUT DATA" TILES=build/assets/TILES.BIN TITLE=build/assets/TITLE.BIN OVER=build/assets/OVER.BIN WIN=build/assets/WIN.BIN SOUND=build/assets/SOUND.BIN

src/data.c src/data.h src/sprites.c src/sprites.h $(filter-out %SOUND.BIN,$(DATA)): tools/convert.py assets/bg.bmp assets/gen_art.h assets/palette.json
	python3 tools/convert.py

build/assets/SOUND.BIN: tools/make_sound.py $(wildcard assets/audio/*)
	python3 tools/make_sound.py

clean:
	rm -rf build

.PHONY: all clean
