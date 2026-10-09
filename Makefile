# CTerm - cross-built with Bebbo's m68k-amigaos-gcc (in ~/opt/amiga);
# the emulator core and its tests also build natively on Linux.

CC      = $(HOME)/opt/amiga/bin/m68k-amigaos-gcc
CFLAGS  = -O2 -m68020 -noixemul -fomit-frame-pointer -Wall -Wno-pointer-sign

all: test

# The core on the PC, against pyte (needs: python3 -m venv .venv &&
# .venv/bin/pip install pyte).
tests/vtdump: tests/vtdump.c src/vt.c src/vt.h
	gcc -O2 -Wall -Wextra -o $@ tests/vtdump.c src/vt.c

test: tests/vtdump
	.venv/bin/python tests/compare.py

clean:
	rm -f tests/vtdump CTerm
