# ----------------------------
# Undertale for the TI-84 Plus CE: the GameMaker VM (../vm) and its
# calculator platform layer (src/). Game data: tools/cepack.py.
# ----------------------------

NAME = UNDERTLE
DESCRIPTION = "Undertale"
COMPRESSED = YES
ARCHIVED = YES
HAS_PRINTF = NO
ALLOCATOR = CUSTOM
# ce/src/float.s replaces the boot ROM's __fadd, __fsub and __fmul (the C runtime
# library defines them too, as ROM addresses): the first definition wins
EXTRA_LDFLAGS = -z muldefs
EXTRA_PRE_LDFLAGS = -z muldefs

EXTRA_C_SOURCES = ../vm/native.c ../vm/pack.c ../vm/value.c ../vm/interp.c ../vm/runtime.c ../vm/draw.c

# Code that runs from the archive (tools/farcode.py), in the AppVar UTFAR:
# the program only has about 150 KB of RAM.
FAR_SOURCES = ../vm/builtins.c ../vm/gmmath.c

CFLAGS = -Wall -Wextra -Oz -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers
CXXFLAGS = $(CFLAGS)

include $(shell cedev-config --makefile)

# The interpreter's hot path in assembly (src/vmfast.s); `make NOFAST=1`
# leaves everything to the C interpreter, for comparing.
ifneq ($(NOFAST),1)
CFLAGS += -DVM_FAST
endif

# `make DEBUG=1` shows frame time (ms), VM instructions and code cache misses
# per frame in the top left corner.
ifeq ($(DEBUG),1)
CFLAGS += -DCE_DEBUG
endif

# `make BENCH=1` times the renderer's building blocks at startup.
ifeq ($(BENCH),1)
CFLAGS += -DCE_BENCH
endif

# `make TEST=<frames>`: a deterministic run that stops after that many
# frames with a hash of the game state (tools/cetest.py).
ifneq ($(TEST),)
CFLAGS += -DCE_TEST=$(TEST)
endif
ifneq ($(TIME_FROM),)
CFLAGS += -DCE_TIME_FROM=$(TIME_FROM)
endif
ifneq ($(PROF_FROM),)
CFLAGS += -DCE_PROF_FROM=$(PROF_FROM)
endif
ifeq ($(FTEST),1)
CFLAGS += -DCE_FTEST -DCE_BENCH
endif
ifeq ($(NODRAW),1)
CFLAGS += -DCE_NODRAW
endif
ifneq ($(TEST_INST),)
CFLAGS += -DCE_TEST_INST=$(TEST_INST)
endif

# `make PROFILE=1` counts CPU cycles per VM opcode (read with tools/ceprof.py).
ifeq ($(PROFILE),1)
CFLAGS += -DCE_PROFILE
endif

# `make` builds the far code's call table, then the program, then links the
# far code against the program (bin/UTFAR.8xv).
.DEFAULT_GOAL := all
.PHONY: all far_prep
all: far_prep
	$(Q)$(MAKE) --no-print-directory target
	$(Q)python3 ../tools/farcode.py link obj/far bin/$(NAME).obj bin/UTFAR.8xv UTFAR
far_prep:
	$(Q)python3 ../tools/farcode.py prep obj/far src -Isrc $(CFLAGS) -- $(FAR_SOURCES)
