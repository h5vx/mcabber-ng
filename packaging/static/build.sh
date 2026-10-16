#!/bin/sh
# Static build of mcabber-ng, in an Alpine Linux (musl) container.
#
#   docker run --rm -v "$PWD:/src" -w /src alpine:3.21 \
#     sh packaging/static/build.sh VERSION
#
# The result is dist/mcabber-ng-VERSION-linux-ARCH-static.tar.gz (or in
# $OUT): the
# binary, the help files, the Lua themes and the sample configuration,
# to be extracted in /usr/local.
#
# Modules, PGP (gpgme) and IDN are disabled in the static binary.

set -eu

VERSION=${1:-dev}
LOUDMOUTH_VERSION=1.5.4
SRC=$(pwd)
OUT=${OUT:-$SRC/dist}
DEPS=/opt/deps
WORK=$(mktemp -d)
ARCH=$(uname -m)

apk add --no-cache \
  build-base autoconf automake libtool pkgconf curl tar file gtk-doc \
  gettext-dev gettext-static \
  ncurses-dev ncurses-static \
  glib-dev glib-static libffi-dev pcre2-dev \
  openssl-dev openssl-libs-static zlib-dev zlib-static

# LuaJIT is not available on every architecture
if apk add --no-cache luajit-dev; then
  LUA=luajit
else
  apk add --no-cache lua5.1-dev
  LUA=lua5.1
fi

# Loudmouth, as a static library
cd "$WORK"
curl -fsSL -o loudmouth.tar.gz \
  "https://github.com/mcabber/loudmouth/archive/refs/tags/$LOUDMOUTH_VERSION.tar.gz"
tar xzf loudmouth.tar.gz
cd "loudmouth-$LOUDMOUTH_VERSION"
autoreconf -fi
./configure --prefix="$DEPS" --enable-static --disable-shared \
  --with-ssl=openssl --without-idn --disable-gtk-doc
make -j"$(nproc)"
make install

# mcabber-ng
cp -a "$SRC/mcabber" "$WORK/mcabber-ng"
cd "$WORK/mcabber-ng"
autoreconf -fi -I macros
PKG_CONFIG="pkg-config --static" \
PKG_CONFIG_PATH="$DEPS/lib/pkgconfig" \
LDFLAGS="-static" \
  ./configure --prefix=/usr/local --with-lua="$LUA" \
    --disable-modules --disable-gpgme --without-libidn --disable-hgcset
make -j"$(nproc)"
make check
strip mcabber/mcabber-ng
file mcabber/mcabber-ng
if ! file mcabber/mcabber-ng | grep -q "statically linked\|static-pie"; then
  echo "The binary is not static!" >&2
  exit 1
fi
./mcabber/mcabber-ng -V

STAGE="$WORK/stage"
make install DESTDIR="$STAGE"
rm -rf "$STAGE/usr/local/include" "$STAGE/usr/local/lib"
cp README "$STAGE/usr/local/share/mcabber-ng/"
cp COPYING "$STAGE/usr/local/share/mcabber-ng/"

NAME="mcabber-ng-$VERSION-linux-$ARCH-static"
mkdir -p "$OUT"
tar -C "$STAGE/usr/local" -czf "$OUT/$NAME.tar.gz" .
echo "Built $OUT/$NAME.tar.gz"
