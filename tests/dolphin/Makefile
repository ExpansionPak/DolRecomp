DEVKITPRO ?= C:/devkitPro
DEVKITPPC ?= $(DEVKITPRO)/devkitPPC
LIBOGC    ?= $(DEVKITPRO)/libogc

PREFIX  := $(DEVKITPPC)/bin/powerpc-eabi-
CC      := $(PREFIX)gcc
ELF2DOL := $(DEVKITPRO)/tools/bin/elf2dol

MACHDEP := -DGEKKO -mcpu=750 -meabi -mhard-float
CFLAGS  := $(MACHDEP) -O0 -Wall -g -I$(LIBOGC)/include
LDFLAGS := $(MACHDEP) -L$(LIBOGC)/lib/cube -Wl,-Map,test_opcodes.map
LIBS    := -logc

all: test_opcodes.dol

test_opcodes.elf: source/main.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS) $(LIBS)

test_opcodes.dol: test_opcodes.elf
	$(ELF2DOL) $< $@

clean:
	rm -f *.elf *.dol *.map

.PHONY: all clean
