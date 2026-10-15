# CTerm - cross-built with Bebbo's m68k-amigaos-gcc (in ~/opt/amiga);
# the emulator core and its tests also build natively on Linux.

CC      = $(HOME)/opt/amiga/bin/m68k-amigaos-gcc
# -fbbb=-: Bebbo's own optimizer pass off for all of CTerm (it was for
# keymap.c only): 0.3b39's click into a pane under the scratchpad never
# reached pane_activate with it on - seen on the A1200, gone with it off.
CFLAGS  = -O2 -m68020 -noixemul -fomit-frame-pointer -fbbb=- -Wall -Wno-pointer-sign -Iinclude

all: CTerm CTermSSH protocols parts test

ifdef PROFILE
CFLAGS += -DPROFILE
endif
ifdef PROFILE_ECLOCK
CFLAGS += -DPROFILE_ECLOCK
endif

TP = third_party
SSHINC = -I$(TP)/amiga -I$(TP)/AmiSSL/Developer/include -I$(TP)/libssh2-1.11.1/include
SSHLIB = $(TP)/lib/libssh2.a -L$(TP)/AmiSSL/Developer/lib/AmigaOS3 -lamisslstubs -lm

SRC = src/main.c src/hide.c src/pane.c src/draw.c src/ppaint.c src/fonts.c src/screen.c src/net.c src/keys.c src/clip.c src/find.c src/drop.c src/cmap.c src/transfer.c src/detect.c src/vt.c src/console.c src/book.c src/serial.c src/classes.c src/parts.c
HDR = src/cmap.h src/ppaint.h src/cterm.h src/xfer.h src/xfermod.h src/vt.h src/boxtable.h src/console.h src/ssh.h src/book.h src/serial.h src/sermod.h src/part.h src/partlist.h src/partuse.h

# CTerm: everything but ssh (small enough for a stock A1200)
# -flto: the parts (split into files) inlined across files again - the
# split had cost a stock A1200 ~1 s of conbench's 42 (measured 2026-10-10)
CTerm: $(SRC) $(HDR) src/ppaint.o src/keymap.o src/ver.o
	$(CC) $(CFLAGS) -flto -DNO_SSH src/ver.o $(SRC) src/ppaint.o src/keymap.o -o CTerm

# the version string first in the code, not through -flto (src/ver.c)
src/ver.o: src/ver.c
	$(CC) -c -noixemul -m68020 src/ver.c -o src/ver.o

# The keys (keymap.c) on their own, without bebbo's backend pass: it
# compiled keys_find's loop as cmp.w (a0)+ and then a0 used as if it had
# not moved (Knowledge/amiga/toolchain-and-testing.md, -fbbb=-)
src/keymap.o: src/keymap.c $(HDR)
	$(CC) $(CFLAGS) -fbbb=- -c src/keymap.c -o $@

# CTermSSH: the same with ssh (libssh2 on AmiSSL), started by CTerm for ssh
CTermSSH: $(SRC) $(HDR) src/ssh.c src/ppaint.o src/keymap.o src/ver.o $(TP)/lib/libssh2.a
	$(CC) $(CFLAGS) $(SSHINC) -c src/ssh.c -o src/ssh.o
	$(CC) $(CFLAGS) -flto src/ver.o $(SRC) src/ssh.o src/ppaint.o src/keymap.o $(SSHLIB) -o CTermSSH

# The hand-written inner loops (vasm, Motorola syntax)
src/ppaint.o: src/ppaint.s
	vasmm68k_mot -Faout -m68020 -quiet -o $@ $<

# The tour: what CTerm does, page by page, and a race against CON:
tour: CTermTour
CTermTour: tools/tour.c
	$(CC) $(CFLAGS) tools/tour.c -o $@

# ShowPic: a picture file shown in CTerm (iTerm2's code), as imgcat
showpic: ShowPic
ShowPic: tools/showpic.c
	$(CC) $(CFLAGS) tools/showpic.c -o $@

# The planar painter's asm against its C reference, under vamos
tests/pptest: tests/pptest.c src/ppaint.c src/ppaint.h src/ppaint.o
	$(CC) $(CFLAGS) tests/pptest.c src/ppaint.c src/ppaint.o -o $@
pptest: tests/pptest
	vamos -C 68020 -m 8192 tests/pptest 3000

# libssh2 and the AmiSSL SDK (downloaded by third_party/fetch.sh)
$(TP)/lib/libssh2.a:
	$(TP)/fetch.sh
	$(TP)/build-libssh2.sh

# Onto the A1200, through Wasabi.
# The transfer protocols: files of their own, loaded when a transfer starts
# (src/xfermod.h). No C start-up: modstart.c is their way in.
MODFLAGS = -O2 -m68020 -noixemul -nostartfiles -fomit-frame-pointer -Wno-pointer-sign
protocols: Protocols/Zmodem Protocols/XYmodem Protocols/Hydra Protocols/Serial
Protocols/Zmodem: src/modstart.c src/zmodem.c src/xfer.h src/xfermod.h
	mkdir -p Protocols
	$(CC) $(MODFLAGS) -Wall -DMOD_ZMODEM src/modstart.c src/zmodem.c -o $@
Protocols/XYmodem: src/modstart.c src/xymodem.c src/xfer.h src/xfermod.h
	mkdir -p Protocols
	$(CC) $(MODFLAGS) -Wall -DMOD_XYMODEM src/modstart.c src/xymodem.c -o $@
Protocols/Hydra: src/modstart.c src/hydraenv.c src/hydra.c src/hydra.h src/hydraenv.h src/xfer.h src/xfermod.h
	mkdir -p Protocols
	$(CC) $(MODFLAGS) -w -DMOD_HYDRA src/modstart.c src/hydraenv.c src/hydra.c -o $@
# The serial line's code (sermod.h): loaded when a serial tab opens its line
Protocols/Serial: src/sermod.c src/sermod.h src/serial.h
	mkdir -p Protocols
	$(CC) $(MODFLAGS) -Wall src/sermod.c -o $@

# The parts (part.h): windows and features loaded when used, from
# PROGDIR:Parts/, gone when done with. A part is CTerm's own source built
# with -DPART: it reaches CTerm only through partlist.h's names
# (partuse.h, made from it here). No C start-up: partstart.c is the way
# in. A part must not get a library base of its own from libnix (it
# would be NULL: nothing opens it): checked after the link.
NM = $(HOME)/opt/amiga/bin/m68k-amigaos-nm
PARTFLAGS = -Os -m68020 -noixemul -nostartfiles -fomit-frame-pointer -Wall -Wno-pointer-sign -Iinclude -DPART
PARTHDR = src/cterm.h src/part.h src/partlist.h src/partuse.h src/vt.h src/serial.h src/book.h
PARTS = Parts/Settings Parts/PhoneBook Parts/ARexx Parts/Call Parts/Transfers Parts/SpeedTest Parts/Files Parts/Pictures
parts: $(PARTS)
src/partuse.h: src/partlist.h
	sed -n -e 's/^V(\(.*\))$$/#define \1 (*penv->\1)/p' -e 's/^A(\(.*\))$$/#define \1 (penv->\1)/p' -e 's/^F(\(.*\))$$/#define \1 (penv->\1)/p' $< > $@
define partlink
	mkdir -p Parts
	$(CC) $(PARTFLAGS) src/partstart.c $(1) -o $@
	@if $(NM) $@ | grep -E ' D _[A-Za-z]+Base$$'; then echo "$@: libnix's library base (above)"; rm -f $@; false; fi
endef
Parts/Settings: src/partstart.c src/setwin.c src/serwin.c $(PARTHDR)
	$(call partlink,src/setwin.c src/serwin.c)
Parts/PhoneBook: src/partstart.c src/bookwin.c src/serwin.c src/askwin.c $(PARTHDR)
	$(call partlink,src/bookwin.c src/serwin.c src/askwin.c)
Parts/ARexx: src/partstart.c src/rexx.c $(PARTHDR)
	$(call partlink,src/rexx.c)
Parts/Call: src/partstart.c src/callwin.c $(PARTHDR)
	$(call partlink,src/callwin.c)
Parts/Transfers: src/partstart.c src/xwin.c $(PARTHDR)
	$(call partlink,src/xwin.c)
Parts/SpeedTest: src/partstart.c src/bench.c $(PARTHDR)
	$(call partlink,src/bench.c)
# the file browser: SFTP (CTermSSH's ssh.c, through sftp_ops) and FTP.
# -fbbb=-: without bebbo's extra backend pass, which miscompiled the
# panels (a post-increment, then the register used as if it had not
# moved: p->top - p->path[] passed for p->cur - p->top)
# pictures in the terminal: iTerm2's files by datatypes, pens for plain
# and 8-bit screens
Parts/Pictures: src/partstart.c src/pics.c src/inflate.c $(PARTHDR)
	$(call partlink,src/pics.c)
Parts/Files: PARTFLAGS += -fbbb=-
Parts/Settings: PARTFLAGS += -fbbb=-
Parts/Files: src/partstart.c src/files.c src/fileui.c src/ftp.c src/files.h src/ssh.h $(PARTHDR)
	$(call partlink,src/files.c src/fileui.c src/ftp.c)

deploy: CTerm CTermSSH CTermTour protocols parts
	wasabi mkdir Work:Utils/CTerm 2>/dev/null || true
	wasabi put CTerm Work:Utils/CTerm/CTerm
	wasabi put CTermTour Work:Utils/CTerm/CTermTour
	wasabi put CTermSSH Work:Utils/CTerm/CTermSSH
	wasabi mkdir Work:Utils/CTerm/Protocols 2>/dev/null || true
	wasabi put Protocols/Zmodem Work:Utils/CTerm/Protocols/Zmodem
	wasabi put Protocols/XYmodem Work:Utils/CTerm/Protocols/XYmodem
	wasabi put Protocols/Hydra Work:Utils/CTerm/Protocols/Hydra
	wasabi put Protocols/Serial Work:Utils/CTerm/Protocols/Serial
	wasabi mkdir Work:Utils/CTerm/Parts 2>/dev/null || true
	for p in $(PARTS); do wasabi put $$p Work:Utils/CTerm/$$p || exit 1; done
	wasabi mkdir Work:Utils/CTerm/Rexx 2>/dev/null || true
	wasabi put rexx/bbs-login.rexx Work:Utils/CTerm/Rexx/bbs-login.rexx
	wasabi put rexx/info.rexx Work:Utils/CTerm/Rexx/info.rexx
	wasabi mkdir Work:Utils/CTerm/Fonts 2>/dev/null || true
	wasabi put fonts/PxPlus_IBM_VGA_8x16.ttf Work:Utils/CTerm/Fonts/PxPlus_IBM_VGA_8x16.ttf
	wasabi put fonts/LICENSE.txt Work:Utils/CTerm/Fonts/LICENSE.txt

# The core on the PC, against pyte (needs: python3 -m venv .venv &&
# .venv/bin/pip install pyte).
tests/vtdump: tests/vtdump.c src/vt.c src/vt.h
	gcc -O2 -Wall -Wextra -o $@ tests/vtdump.c src/vt.c

tests/amiga_test: tests/amiga_test.c src/vt.c src/vt.h
	gcc -O2 -Wall -o $@ tests/amiga_test.c src/vt.c

test: tests/vtdump tests/amiga_test tests/shadow_test tests/pic_test tests/link_test tests/cmap_test
	.venv/bin/python tests/compare.py
	tests/amiga_test
	tests/pic_test
	tests/link_test
	tests/cmap_check.sh fonts/PxPlus_IBM_VGA_8x16.ttf $(wildcard /usr/share/fonts/TTF/DejaVuSansMono.ttf)
	.venv/bin/python tests/lines.py
	tests/shadow_test tests/streams/*.bin tests/lines/*.bin

# pictures: sixel and iTerm2 files (tests/pictures/mksixel.py makes them)
tests/pic_test: tests/pic_test.c src/vt.c src/vt.h src/inflate.c
	gcc -O2 -Wall -o $@ tests/pic_test.c src/vt.c src/inflate.c

# hyperlinks (OSC 8) and the wrapped-row mark; a cap of 4 addresses, so
# dropping the unused ones is tried too
tests/link_test: tests/link_test.c src/vt.c src/vt.h
	gcc -O2 -Wall -DLINK_MAX=5 -DLINK_BYTES=200 -o $@ tests/link_test.c src/vt.c

# the fallback font's cmap reader (src/cmap.c) against fontconfig's
tests/cmap_test: tests/cmap_test.c src/cmap.c src/cmap.h
	gcc -O2 -Wall -o $@ tests/cmap_test.c src/cmap.c

tests/shadow_test: tests/shadow_test.c src/vt.c src/vt.h
	gcc -O2 -Wall -o tests/shadow_test tests/shadow_test.c src/vt.c

clean:
	rm -f tests/vtdump CTerm CTermSSH src/*.o $(PARTS)

# Zmodem against lrzsz on the PC (build lrzsz first: tests/zmodem_test.c)
tests/zmodem_test: tests/zmodem_test.c src/zmodem.c src/xymodem.c src/xfer.h
	gcc -O2 -Wall -o $@ tests/zmodem_test.c src/xymodem.c src/detect.c

tests/hydra_test: tests/hydra_test.c src/hydra.c src/hydraenv.c src/detect.c src/hydraenv.h src/xfer.h
	gcc -m32 -std=gnu89 -O2 -w -Wl,--wrap=malloc,--wrap=free -o $@ tests/hydra_test.c src/hydra.c src/hydraenv.c src/detect.c

# all the transfer protocols against their references (lrzsz, HydraCom)
ztest: tests/zmodem_test tests/hydra_test
	tests/zmodem_test
	tests/hydra/build.sh
	tests/hydra_test


# The emulator against random hostile bytes, with ASan/UBSan (a few
# minutes; any report or hang is a bug): make fuzz
tests/fuzz/fz: tests/fuzz/fz.c src/vt.c src/vt.h src/inflate.c
	gcc -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -I. -o $@ tests/fuzz/fz.c src/vt.c src/inflate.c

fuzz: tests/fuzz/fz
	for s in 1 2 3 4 5 6 7 8 9 10; do tests/fuzz/fz $$s || exit 1; done; echo "fuzz: all passed"
