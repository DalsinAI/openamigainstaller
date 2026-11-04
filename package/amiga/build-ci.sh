#!/bin/sh
# OpenInstaller for AmigaOS 3.2 (68040, soft float), built on a Linux CI
# runner with the Team's toolchain and headers, all pinned:
#   - DalsinAI/gfopengcc's release (OpenAmigaGCC 16.2.0-open6), sha256 checked
#   - DalsinAI/gfopensdk (OpenNDK headers, the soft-float specs) at SDK_COMMIT
#   - DalsinAI/gfopenlayout's headers (gfopenlayout.library 11's calls) at
#     LAYOUT_COMMIT
# and, with build-curl.sh, libcurl (built into the program, for (download))
# and gfopentls.library (HTTPS). Then packs
# OpenInstaller-<version>-amiga-68040.lha: the OpenInstaller drawer with the
# program, Libs/gfopentls.library, S/GFOpenTLS/ca-bundle.pem, the
# licences, the guide and the example scripts with their pictures, and
# Install-OpenInstaller, which the Installer AmigaOS 3.2 has runs.
#
#   package/amiga/build-ci.sh VERSION OUTDIR
set -eu
VERSION=${1:?version}
DIST=${2:?out dir}
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../.." && pwd)
WORK=${WORK:-$ROOT/build-amiga}

GCC_TAG=v16.2.0-open6
GCC_TAR=gfopenamigagcc-16.2.0-open6-x86_64-linux.tar.xz
GCC_SHA=65f284476ec20cdf7851475d91d2c18d77fa6f2a0eb0f844b6ca23dbfa57eaa7
SDK_COMMIT=a7384f334c3eb64b30dd27cc5a6f480d72471f74
LAYOUT_COMMIT=5f1638b6490171b58cb3aeee29b9a7f18ca7e756

mkdir -p "$WORK" "$DIST"
DIST=$(CDPATH= cd -- "$DIST" && pwd)
cd "$WORK"

# 1. The toolchain (TC: one already unpacked, for a machine that has it).
if [ -n "${TC:-}" ]; then
    TC=$(CDPATH= cd -- "$TC" && pwd)
else
    if [ ! -x gcc/bin/m68k-amigaos-gcc ]; then
        curl -fsSL -o "$GCC_TAR" "https://github.com/DalsinAI/gfopengcc/releases/download/$GCC_TAG/$GCC_TAR"
        echo "$GCC_SHA  $GCC_TAR" | sha256sum -c -
        mkdir -p gcc
        tar xJf "$GCC_TAR" -C gcc --strip-components=1
    fi
    TC=$WORK/gcc
fi

# 2. OpenNDK and gfopenlayout's headers.
fetch() {   # fetch REPO COMMIT DIR
    if [ -L "$3" ]; then rm "$3"; fi
    if [ ! -d "$3/.git" ]; then
        git init -q "$3"
        git -C "$3" remote add origin "https://github.com/DalsinAI/$1"
    fi
    git -C "$3" fetch -q --depth 1 origin "$2"
    git -C "$3" checkout -q -f FETCH_HEAD
}
fetch gfopensdk "$SDK_COMMIT" sdk
fetch gfopenlayout "$LAYOUT_COMMIT" gfopenlayout
# build-curl.sh's gfopencurl build wants OpenNDK in the toolchain's include
# directory; OpenInstaller itself names it with -isystem.
cp -R sdk/OpenNDK/Include_H/. "$TC/m68k-amigaos/include/"
[ -f "$TC/m68k-amigaos/include/proto/socket.h" ] ||
    printf '#include <proto/bsdsocket.h>\n' > "$TC/m68k-amigaos/include/proto/socket.h"

# 3. libcurl and gfopentls.library (build-curl.sh).
sh "$HERE/build-curl.sh" "$TC" "$WORK/sdk/OpenNDK" "$WORK/net"

# 4. OpenInstaller, with libcurl in it.
make -C "$ROOT" amiga AMIGA_CC="$TC/bin/m68k-amigaos-gcc" AMIGA_SDK="$WORK/sdk" \
    AMIGA_LAYOUT="$WORK/gfopenlayout" AMIGA_CURL="$WORK/net"
"$TC/bin/m68k-amigaos-size" "$ROOT/OpenInstaller.68k"
# soft float: the compiler, with the specs the Makefile gives it, has no FPU
if "$TC/bin/m68k-amigaos-gcc" -m68040 -specs="$WORK/sdk/Lib/m68k-soft-m68040.specs" -dM -E - < /dev/null |
        grep -q __HAVE_68881__; then
    echo "OpenInstaller must be soft float"
    exit 1
fi
echo "Soft float: yes"

# 5. The archive: the OpenInstaller drawer, and Install-OpenInstaller beside
#    it for the Installer AmigaOS 3.2 has. The icons are make_icons.py's.
PKG=$WORK/pkg
I=$HERE/files/Icons
rm -rf "$PKG"
D=$PKG/OpenInstaller
mkdir -p "$D/Libs" "$D/S/GFOpenTLS" "$D/Licences" "$D/Examples/Pictures"
cp "$ROOT/OpenInstaller.68k" "$D/OpenInstaller"
cp "$ROOT/README.md" "$ROOT/LICENSE" "$ROOT/docs/Guide.md" "$ROOT/docs/Extras.md" "$D/"
for f in README.md Guide.md Extras.md LICENSE Examples; do cp "$I/$f.info" "$D/"; done
for f in Install-Stages Install-Download Install-Both; do
    cp "$ROOT/examples/$f" "$I/$f.info" "$D/Examples/"
done
cp "$I/Pictures.info" "$D/Examples/"
cp "$ROOT"/examples/Pictures/* "$D/Examples/Pictures/"
cp "$WORK/net/gfopentls.library" "$D/Libs/"
[ ! -f "$WORK/net/ca-bundle.pem" ] || cp "$WORK/net/ca-bundle.pem" "$D/S/GFOpenTLS/"
cp "$WORK"/net/Licences/* "$D/Licences/"
printf '%s\n' "$VERSION" > "$D/VERSION"
cp "$I/OpenInstaller.info" "$PKG/"
sed "s/^(set #version \"[^\"]*\")/(set #version \"$VERSION\")/; s/^; \$VER: Install-OpenInstaller [^ ]*/; \$VER: Install-OpenInstaller $VERSION/" \
    "$HERE/files/Install-OpenInstaller" > "$PKG/Install-OpenInstaller"
cp "$I/Install-OpenInstaller.info" "$PKG/"
if [ -n "${PKG_OUT:-}" ]; then
    mkdir -p "$PKG_OUT" && cp -R "$PKG"/. "$PKG_OUT"/
fi
[ -z "${NO_LHA:-}" ] || exit 0
LHA=$(command -v lha || command -v jlha)
NAME=OpenInstaller-$VERSION-amiga-68040.lha
rm -f "$DIST/$NAME"
(cd "$PKG" && "$LHA" a "$DIST/$NAME" OpenInstaller OpenInstaller.info Install-OpenInstaller Install-OpenInstaller.info)
"$LHA" l "$DIST/$NAME"
ls -l "$DIST/$NAME"
