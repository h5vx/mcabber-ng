#!/bin/sh
# Universal Debian/Ubuntu package from the static build: the binary has no
# dependencies, so the same package installs on any Debian-based system.
#
#   packaging/static/make-deb.sh VERSION ARCH TARBALL
#
# ARCH is the name used by build.sh (x86_64, x86, aarch64, armv7, riscv64);
# TARBALL is mcabber-ng-VERSION-linux-ARCH-static.tar.gz.  The package is
# written to dist/ (or $OUT).  Needs dpkg-deb.

set -eu

VERSION=$1
ARCH=$2
TARBALL=$3
SRC=$(pwd)
OUT=${OUT:-$SRC/dist}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

case "$ARCH" in
  x86_64)  DEBARCH=amd64 ;;
  x86)     DEBARCH=i386 ;;
  aarch64) DEBARCH=arm64 ;;
  armv7)   DEBARCH=armhf ;;
  riscv64) DEBARCH=riscv64 ;;
  *) echo "Unknown architecture: $ARCH" >&2; exit 1 ;;
esac
# No "-" in a native package version
DEBVERSION=$(echo "$VERSION" | tr - '~')

PKG="$WORK/pkg"
mkdir -p "$PKG/usr" "$PKG/DEBIAN"
tar -C "$PKG/usr" -xzf "$TARBALL"

# Documentation in /usr/share/doc, compressed manual page
DOC="$PKG/usr/share/doc/mcabber-ng"
mkdir -p "$DOC"
mv "$PKG/usr/share/mcabber-ng/README" "$DOC/README"
rm -f "$PKG/usr/share/mcabber-ng/COPYING"
cat "$SRC/packaging/debian/copyright" > "$DOC/copyright"
cat >> "$DOC/copyright" <<'EOF'

Comment: The program is statically linked with GLib, ncurses, Loudmouth,
 OpenSSL, LuaJIT (or Lua 5.1), musl, libomemo-c (GPL-3), libgcrypt and
 libgpg-error (LGPL-2.1+), protobuf-c and their dependencies; the binary
 is therefore distributed under the terms of the GPL-3.
EOF
gzip -9n "$PKG/usr/share/man/man1/mcabber-ng.1"

cat > "$PKG/DEBIAN/control" <<EOF
Package: mcabber-ng
Version: $DEBVERSION
Architecture: $DEBARCH
Maintainer: h5vx <h5vx@users.noreply.github.com>
Installed-Size: $(du -sk "$PKG/usr" | cut -f1)
Section: net
Priority: optional
Homepage: https://github.com/h5vx/mcabber-ng
Description: Jabber (XMPP) console client configured in Lua
 mcabber-ng is a fork of mcabber, a small Jabber (XMPP) client for the
 console.  Its configuration is a Lua script: options, key bindings with
 readable names, event handlers, message and interface formats, themes.
 .
 It supports multi-line messages, does not send pasted text line by line,
 has a configurable interface with frames and 256/24-bit colors, and
 OMEMO end-to-end encryption.
 .
 This package contains a static build, without dependencies, for any
 Debian or Ubuntu release.  It is built without modules, PGP, OTR, IDN
 and spell checking.
EOF

chmod -R u+rwX,go+rX,go-w "$PKG"

mkdir -p "$OUT"
DEB="$OUT/mcabber-ng_${DEBVERSION}_$DEBARCH.deb"
dpkg-deb --root-owner-group -Zxz --build "$PKG" "$DEB"
echo "Built $DEB"
