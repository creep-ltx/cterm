# CTerm - cross-built with Bebbo's m68k-amigaos-gcc (in ~/opt/amiga);
# the emulator core and its tests also build natively on Linux.

CC      = $(HOME)/opt/amiga/bin/m68k-amigaos-gcc
CFLAGS  = -O2 -m68020 -noixemul -fomit-frame-pointer -Wall -Wno-pointer-sign -Iinclude

all: CTerm CTermSSH protocols test

ifdef PROFILE
CFLAGS += -DPROFILE
endif
ifdef PROFILE_ECLOCK
CFLAGS += -DPROFILE_ECLOCK
endif

TP = third_party
SSHINC = -I$(TP)/amiga -I$(TP)/AmiSSL/Developer/include -I$(TP)/libssh2-1.11.1/include
SSHLIB = $(TP)/lib/libssh2.a -L$(TP)/AmiSSL/Developer/lib/AmigaOS3 -lamisslstubs -lm

SRC = src/main.c src/draw.c src/fonts.c src/screen.c src/net.c src/keys.c src/clip.c src/setwin.c src/bench.c src/transfer.c src/detect.c src/vt.c src/console.c src/book.c
HDR = src/cterm.h src/xfer.h src/xfermod.h src/vt.h src/boxtable.h src/console.h src/ssh.h src/book.h

# CTerm: everything but ssh (small enough for a stock A1200)
# -flto: the parts (split into files) inlined across files again - the
# split had cost a stock A1200 ~1 s of conbench's 42 (measured 2026-10-10)
CTerm: $(SRC) $(HDR)
	$(CC) $(CFLAGS) -flto -DNO_SSH $(SRC) -o CTerm

# CTermSSH: the same with ssh (libssh2 on AmiSSL), started by CTerm for ssh
CTermSSH: $(SRC) $(HDR) src/ssh.c $(TP)/lib/libssh2.a
	$(CC) $(CFLAGS) $(SSHINC) -c src/ssh.c -o src/ssh.o
	$(CC) $(CFLAGS) -flto $(SRC) src/ssh.o $(SSHLIB) -o CTermSSH

# libssh2 and the AmiSSL SDK (downloaded by third_party/fetch.sh)
$(TP)/lib/libssh2.a:
	$(TP)/fetch.sh
	$(TP)/build-libssh2.sh

# Onto the A1200, through Wasabi.
# The transfer protocols: files of their own, loaded when a transfer starts
# (src/xfermod.h). No C start-up: modstart.c is their way in.
MODFLAGS = -O2 -m68020 -noixemul -nostartfiles -fomit-frame-pointer -Wno-pointer-sign
protocols: Protocols/Zmodem Protocols/XYmodem Protocols/Hydra
Protocols/Zmodem: src/modstart.c src/zmodem.c src/xfer.h src/xfermod.h
	mkdir -p Protocols
	$(CC) $(MODFLAGS) -Wall -DMOD_ZMODEM src/modstart.c src/zmodem.c -o $@
Protocols/XYmodem: src/modstart.c src/xymodem.c src/xfer.h src/xfermod.h
	mkdir -p Protocols
	$(CC) $(MODFLAGS) -Wall -DMOD_XYMODEM src/modstart.c src/xymodem.c -o $@
Protocols/Hydra: src/modstart.c src/hydraenv.c src/hydra.c src/hydra.h src/hydraenv.h src/xfer.h src/xfermod.h
	mkdir -p Protocols
	$(CC) $(MODFLAGS) -w -DMOD_HYDRA src/modstart.c src/hydraenv.c src/hydra.c -o $@

deploy: CTerm CTermSSH protocols
	wasabi mkdir Work:Utils/CTerm 2>/dev/null || true
	wasabi put CTerm Work:Utils/CTerm/CTerm
	wasabi put CTermSSH Work:Utils/CTerm/CTermSSH
	wasabi mkdir Work:Utils/CTerm/Protocols 2>/dev/null || true
	wasabi put Protocols/Zmodem Work:Utils/CTerm/Protocols/Zmodem
	wasabi put Protocols/XYmodem Work:Utils/CTerm/Protocols/XYmodem
	wasabi put Protocols/Hydra Work:Utils/CTerm/Protocols/Hydra
	wasabi mkdir Work:Utils/CTerm/Fonts 2>/dev/null || true
	wasabi put fonts/PxPlus_IBM_VGA_8x16.ttf Work:Utils/CTerm/Fonts/PxPlus_IBM_VGA_8x16.ttf
	wasabi put fonts/LICENSE.txt Work:Utils/CTerm/Fonts/LICENSE.txt

# The core on the PC, against pyte (needs: python3 -m venv .venv &&
# .venv/bin/pip install pyte).
tests/vtdump: tests/vtdump.c src/vt.c src/vt.h
	gcc -O2 -Wall -Wextra -o $@ tests/vtdump.c src/vt.c

tests/amiga_test: tests/amiga_test.c src/vt.c src/vt.h
	gcc -O2 -Wall -o $@ tests/amiga_test.c src/vt.c

test: tests/vtdump tests/amiga_test tests/shadow_test
	.venv/bin/python tests/compare.py
	tests/amiga_test
	.venv/bin/python tests/lines.py
	tests/shadow_test tests/streams/*.bin tests/lines/*.bin

tests/shadow_test: tests/shadow_test.c src/vt.c src/vt.h
	gcc -O2 -Wall -o tests/shadow_test tests/shadow_test.c src/vt.c

clean:
	rm -f tests/vtdump CTerm CTermSSH src/*.o

# Zmodem against lrzsz on the PC (build lrzsz first: tests/zmodem_test.c)
tests/zmodem_test: tests/zmodem_test.c src/zmodem.c src/xymodem.c src/xfer.h
	gcc -O2 -Wall -o $@ tests/zmodem_test.c src/zmodem.c src/xymodem.c src/detect.c

tests/hydra_test: tests/hydra_test.c src/hydra.c src/hydraenv.c src/detect.c src/hydraenv.h src/xfer.h
	gcc -m32 -std=gnu89 -O2 -w -o $@ tests/hydra_test.c src/hydra.c src/hydraenv.c src/detect.c

# all the transfer protocols against their references (lrzsz, HydraCom)
ztest: tests/zmodem_test tests/hydra_test
	tests/zmodem_test
	tests/hydra/build.sh
	tests/hydra_test

