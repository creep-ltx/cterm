#!/usr/bin/env bash
# build-libssh2.sh - libssh2 for AmigaOS 3 (68020) into third_party/lib/libssh2.a,
# with the OpenSSL backend going through AmiSSL and sockets through bsdsocket.
set -e
here=$(cd "$(dirname "$0")" && pwd)
src=$here/libssh2-1.11.1/src
out=$here/obj; mkdir -p "$out" "$here/lib"
CC=$HOME/opt/amiga/bin/m68k-amigaos-gcc
AR=$HOME/opt/amiga/bin/m68k-amigaos-ar
CFLAGS="-O2 -m68020 -noixemul -fomit-frame-pointer -DHAVE_CONFIG_H \
  -I$here/amiga -I$here/AmiSSL/Developer/include -I$here/libssh2-1.11.1/include \
  -include $here/amiga/prelude.h -w"
objs=()
for f in agent bcrypt_pbkdf channel comp crypt crypto global hostkey keepalive kex \
         knownhost mac misc packet pem publickey scp session sftp transport userauth \
         userauth_kbd_packet version openssl blowfish chacha cipher-chachapoly poly1305; do
  [ -f "$src/$f.c" ] || continue
  $CC $CFLAGS -c "$src/$f.c" -o "$out/$f.o"
  objs+=("$out/$f.o")
done
rm -f "$here/lib/libssh2.a"
$AR rcs "$here/lib/libssh2.a" "${objs[@]}"
echo "built $here/lib/libssh2.a (${#objs[@]} objects)"
