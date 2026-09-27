# ----------------------------
# Undertale intro for the TI-84 Plus CE
# ----------------------------

NAME = UNDERTLE
DESCRIPTION = "Undertale Intro"
COMPRESSED = NO
ARCHIVED = YES
HAS_PRINTF = NO

CFLAGS = -Wall -Wextra -Oz
CXXFLAGS = $(CFLAGS)

include $(shell cedev-config --makefile)

# `make AUTOPLAY=1` plays the scripted input in src/autoplay.h (for testing).
ifeq ($(AUTOPLAY),1)
CFLAGS += -DCE_AUTOPLAY
endif

ifeq ($(FLOWEY_TEST),1)
CFLAGS += -DCE_AUTOPLAY -DCE_FLOWEY_TEST
endif
