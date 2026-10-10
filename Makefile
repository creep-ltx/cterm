# CTerm - cross-built with Bebbo's m68k-amigaos-gcc (in ~/opt/amiga);
# the emulator core and its tests also build natively on Linux.

CC      = $(HOME)/opt/amiga/bin/m68k-amigaos-gcc
CFLAGS  = -O2 -m68020 -noixemul -fomit-frame-pointer -Wall -Wno-pointer-sign -Iinclude

all: CTerm CTermSSH test

ifdef PROFILE
CFLAGS += -DPROFILE
endif
ifdef PROFILE_ECLOCK
CFLAGS += -DPROFILE_ECLOCK
endif

TP = third_party
SSHINC = -I$(TP)/amiga -I$(TP)/AmiSSL/Developer/include -I$(TP)/libssh2-1.11.1/include
SSHLIB = $(TP)/lib/libssh2.a -L$(TP)/AmiSSL/Developer/lib/AmigaOS3 -lamisslstubs -lm

SRC = src/main.c src/vt.c src/console.c
HDR = src/vt.h src/boxtable.h src/console.h src/ssh.h

# CTerm: everything but ssh (small enough for a stock A1200)
CTerm: $(SRC) $(HDR)
	$(CC) $(CFLAGS) -DNO_SSH $(SRC) -o CTerm

# CTermSSH: the same with ssh (libssh2 on AmiSSL), started by CTerm for ssh
CTermSSH: $(SRC) $(HDR) src/ssh.c $(TP)/lib/libssh2.a
	$(CC) $(CFLAGS) $(SSHINC) -c src/ssh.c -o src/ssh.o
	$(CC) $(CFLAGS) $(SRC) src/ssh.o $(SSHLIB) -o CTermSSH

# libssh2 and the AmiSSL SDK (downloaded by third_party/fetch.sh)
$(TP)/lib/libssh2.a:
	$(TP)/fetch.sh
	$(TP)/build-libssh2.sh

# Onto the A1200, through Wasabi.
deploy: CTerm CTermSSH
	wasabi mkdir Work:Utils/CTerm 2>/dev/null || true
	wasabi put CTerm Work:Utils/CTerm/CTerm
	wasabi put CTermSSH Work:Utils/CTerm/CTermSSH

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
	rm -f tests/vtdump CTerm CTermSSH src/*.o
