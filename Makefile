# CTerm - cross-built with Bebbo's m68k-amigaos-gcc (in ~/opt/amiga);
# the emulator core and its tests also build natively on Linux.

CC      = $(HOME)/opt/amiga/bin/m68k-amigaos-gcc
CFLAGS  = -O2 -m68020 -noixemul -fomit-frame-pointer -Wall -Wno-pointer-sign -Iinclude

all: CTerm test

ifdef PROFILE
CFLAGS += -DPROFILE
endif
ifdef PROFILE_ECLOCK
CFLAGS += -DPROFILE_ECLOCK
endif
ifdef DTDEBUG
CFLAGS += -DDTDEBUG
endif

CTerm: src/main.c src/vt.c src/vt.h src/boxtable.h src/console.c src/console.h
	$(CC) $(CFLAGS) src/main.c src/vt.c src/console.c -o CTerm

# Onto the A1200, through Wasabi.
deploy: CTerm
	wasabi mkdir Work:Utils/CTerm 2>/dev/null || true
	wasabi put CTerm Work:Utils/CTerm/CTerm

# The core on the PC, against pyte (needs: python3 -m venv .venv &&
# .venv/bin/pip install pyte).
tests/vtdump: tests/vtdump.c src/vt.c src/vt.h
	gcc -O2 -Wall -Wextra -o $@ tests/vtdump.c src/vt.c

tests/amiga_test: tests/amiga_test.c src/vt.c src/vt.h
	gcc -O2 -Wall -o $@ tests/amiga_test.c src/vt.c

test: tests/vtdump tests/amiga_test tests/shadow_test
	.venv/bin/python tests/compare.py
	tests/amiga_test
	tests/shadow_test tests/streams/*.bin

tests/shadow_test: tests/shadow_test.c src/vt.c src/vt.h
	gcc -O2 -Wall -o tests/shadow_test tests/shadow_test.c src/vt.c

clean:
	rm -f tests/vtdump CTerm
