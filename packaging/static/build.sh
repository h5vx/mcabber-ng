#!/bin/sh
# Static build of mcabber-ng, in an Alpine Linux (musl) container.
#
#   docker run --rm -v "$PWD:/src" -w /src alpine:3.21 \
#     sh packaging/static/build.sh VERSION
#
# ARCH sets the architecture name used in the file name (default: uname -m).
#
# The result is dist/mcabber-ng-VERSION-linux-ARCH-static.tar.gz (or in
# $OUT): the binary, the help files, the Lua themes and the sample
# configuration, to be extracted in /usr/local.
#
# Modules, PGP (gpgme), IDN and spell checking are disabled in the static
# binary.

set -eu

VERSION=${1:-dev}
LOUDMOUTH_VERSION=1.5.4
LIBOMEMO_C_VERSION=0.5.1
LIBOTR_VERSION=4.1.1
SRC=$(pwd)
OUT=${OUT:-$SRC/dist}
DEPS=/opt/deps
WORK=$(mktemp -d)
ARCH=${ARCH:-$(uname -m)}

apk add --no-cache \
  build-base autoconf automake libtool pkgconf curl tar file gtk-doc \
  gettext-dev gettext-static \
  ncurses-dev ncurses-static \
  glib-dev glib-static libffi-dev pcre2-dev \
  openssl-dev openssl-libs-static zlib-dev zlib-static \
  cmake libgcrypt-dev libgcrypt-static libgpg-error-dev libgpg-error-static \
  protobuf-c-dev

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
# Loudmouth uses -Werror, which fails with recent compilers and GLib
find . -name Makefile -exec sed -i 's/-Werror//g' {} +
make -j"$(nproc)"
make install

# libomemo-c (OMEMO), as a static library
cd "$WORK"
curl -fsSL -o libomemo-c.tar.gz \
  "https://github.com/dino/libomemo-c/archive/refs/tags/v$LIBOMEMO_C_VERSION.tar.gz"
tar xzf libomemo-c.tar.gz
cmake -S "libomemo-c-$LIBOMEMO_C_VERSION" -B libomemo-c-build \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
  -DCMAKE_INSTALL_PREFIX="$DEPS"
cmake --build libomemo-c-build -j"$(nproc)"
cmake --install libomemo-c-build

# libotr (OTR), as a static library
cd "$WORK"
curl -fsSL -o libotr.tar.gz \
  "https://otr.cypherpunks.ca/libotr-$LIBOTR_VERSION.tar.gz"
tar xzf libotr.tar.gz
cd "libotr-$LIBOTR_VERSION"
# Its config.guess (2012) doesn't know recent architectures (riscv64)
cp /usr/share/automake-*/config.guess /usr/share/automake-*/config.sub .
./configure --prefix="$DEPS" --enable-static --disable-shared
# Only the library (the tests don't build with recent compilers)
make -j"$(nproc)" -C src
make -C src install

# mcabber-ng
cp -a "$SRC/mcabber" "$WORK/mcabber-ng"
# The version comes from the release tag
sed -i "s/^AC_INIT(\[mcabber-ng\],\[[^]]*\]/AC_INIT([mcabber-ng],[$VERSION]/" \
  "$WORK/mcabber-ng/configure.ac"
cd "$WORK/mcabber-ng"
autoreconf -fi -I macros
# The static libraries of GLib/GObject (libffi, pcre2...) are added at the
# end of the link command.
PKG_CONFIG="pkg-config --static" \
PKG_CONFIG_PATH="$DEPS/lib/pkgconfig" \
LDFLAGS="-static" \
LIBS="$(pkg-config --static --libs gobject-2.0 glib-2.0 libgcrypt)" \
  ./configure --prefix=/usr/local --with-lua="$LUA" \
    --disable-modules --disable-gpgme --without-libidn --disable-hgcset \
    --enable-omemo=yes --enable-otr \
    --with-libotr-prefix="$DEPS/lib" --with-libotr-inc-prefix="$DEPS/include"
# libtool needs -all-static to link a fully static program
make -j"$(nproc)" LDFLAGS="-all-static"
make check LDFLAGS="-all-static"
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
