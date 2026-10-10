#!/usr/bin/env bash
# fetch.sh - download what CTerm's ssh builds on (not kept in git):
# the AmiSSL 5.27 SDK (headers, link stubs) and libssh2 1.11.1, then apply
# CTerm's Amiga patch to libssh2 (amiga/libssh2-amiga-nonblock.patch).
set -e
cd "$(dirname "$0")"
[ -d AmiSSL/Developer ] || { curl -sL -o AmiSSL-5.27-SDK.lha \
  https://github.com/jens-maus/amissl/releases/download/5.27/AmiSSL-5.27-SDK.lha
  lha xq AmiSSL-5.27-SDK.lha; }
if [ ! -d libssh2-1.11.1 ]; then
  curl -sL -o libssh2-1.11.1.tar.gz https://libssh2.org/download/libssh2-1.11.1.tar.gz
  tar xzf libssh2-1.11.1.tar.gz
  (cd libssh2-1.11.1/src && patch -p0 < ../../amiga/libssh2-amiga-nonblock.patch)
fi
echo "third_party ready: now ./build-libssh2.sh"
