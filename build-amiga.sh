#!/bin/sh
# For AmiCradle's stoves (Cradle's stove_build): STOVE is the stove (its
# prefix/ the toolchain), OUT where the unpacked package goes. It is
# package/amiga/build-ci.sh with that toolchain, and no archive.
set -eu
: "${STOVE:?}" "${OUT:?}"
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mkdir -p "$OUT"
TC="$STOVE/prefix" NO_LHA=1 PKG_OUT="$OUT" JOBS=${JOBS:-8} \
    sh "$HERE/package/amiga/build-ci.sh" "${VERSION:-1.0.0-lab}" "$OUT/dist"
rm -rf "$OUT/dist"
