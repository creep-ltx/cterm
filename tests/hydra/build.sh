#!/usr/bin/env bash
# build.sh - HydraCom 1.00 (Arjen G. Lentz, 1993; github.com/lentzdev/hydracom)
# for Linux as tests/hydra/hcref: CTerm's Hydra is tested against it.
# Its own hydra.c and fmisc.c, unchanged except that a word is 16 bits as
# on MS-DOS (2types.h), with hcshim.c for the MS-DOS parts; 32-bit (-m32),
# so a long is 4 bytes. Needs the source in third_party/src/hydracom.
set -e
cd "$(dirname "$0")"
HC=../../third_party/src/hydracom
[ -d "$HC" ] || git clone -q --depth 1 https://github.com/lentzdev/hydracom "$HC"
B=$(mktemp -d)
cp "$HC"/*.c "$HC"/*.h "$B"/
sed -i 's/^typedef unsigned int  word;/typedef unsigned short word;/' "$B/2types.h"
gcc -m32 -std=gnu89 -O1 -w -D__PROTO__ -D__MSDOS__ -I fakeinc -I "$B" \
    -include fakeinc/dos.h -o hcref hcshim.c "$B/hydra.c" "$B/fmisc.c"
rm -rf "$B"
echo "tests/hydra/hcref built"
