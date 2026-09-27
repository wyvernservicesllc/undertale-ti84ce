# Mac/Linux test build of the Undertale VM.
CC ?= cc
CFLAGS ?= -O2 -g -ffp-contract=off -DVM_TRACE -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers
SRC = ../vm/gmmath.c ../vm/zx0.c ../vm/native.c ../vm/pack.c ../vm/value.c ../vm/interp.c ../vm/runtime.c ../vm/builtins.c ../vm/draw.c host.c

host: $(SRC) ../vm/vm.h ../vm/gen_ids.h
	$(CC) $(CFLAGS) -o $@ $(SRC) -lm

# a window with sound, to play: ./play <pack dir> 0
play: $(SRC) ../vm/vm.h ../vm/gen_ids.h
	$(CC) -O2 -ffp-contract=off -DHOST_SDL $(shell sdl2-config --cflags) -Wno-everything -o $@ $(SRC) -lm $(shell sdl2-config --libs) -lSDL2_mixer

clean:
	rm -f host play
