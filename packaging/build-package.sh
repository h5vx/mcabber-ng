#!/bin/sh
# Build a package of mcabber-ng, in a container of the target distribution
# (run from the top of the repository):
#
#   packaging/build-package.sh deb|rpm|arch VERSION [DISTRIBUTION]
#
# DISTRIBUTION is added to the version of Debian packages (e.g. debian12).
# Packages are written to dist/ (or $OUT).

set -eu

FAMILY=$1
VERSION=$2
DIST=${3:-}
SRC=$(pwd)
OUT=${OUT:-$SRC/dist}
WORK=$(mktemp -d)
TREE="$WORK/mcabber-ng-$VERSION"

mkdir -p "$OUT"
cp -a "$SRC/mcabber" "$TREE"
# The version comes from the release tag
sed -i "s/^AC_INIT(\[mcabber-ng\],\[[^]]*\]/AC_INIT([mcabber-ng],[$VERSION]/" \
  "$TREE/configure.ac"

case "$FAMILY" in
  deb)
    export DEBIAN_FRONTEND=noninteractive
    apt-get update
    apt-get install -y --no-install-recommends \
      build-essential devscripts equivs fakeroot
    cp -a "$SRC/packaging/debian" "$TREE/debian"
    # Native package: no "-" in the version
    DEBVERSION=$(echo "$VERSION" | tr - '~')${DIST:+~$DIST}
    cat > "$TREE/debian/changelog" <<EOT
mcabber-ng ($DEBVERSION) unstable; urgency=medium

  * Release $VERSION.

 -- h5vx <h5vx@users.noreply.github.com>  $(date -R)
EOT
    cd "$TREE"
    mk-build-deps -i -r -t "apt-get -y --no-install-recommends" \
      debian/control
    rm -f mcabber-ng-build-deps_*
    dpkg-buildpackage -us -uc -b
    cp "$WORK"/mcabber-ng_*.deb "$OUT/"
    ;;

  rpm)
    RPMVERSION=$(echo "$VERSION" | tr - '~')
    TREE2="$WORK/mcabber-ng-$RPMVERSION"
    [ "$TREE" = "$TREE2" ] || mv "$TREE" "$TREE2"
    dnf install -y rpm-build dnf-plugins-core tar gzip
    mkdir -p "$HOME/rpmbuild/SOURCES"
    tar -C "$WORK" -czf "$HOME/rpmbuild/SOURCES/mcabber-ng-$RPMVERSION.tar.gz" \
      "mcabber-ng-$RPMVERSION"
    dnf builddep -y --define "pkgversion $RPMVERSION" \
      "$SRC/packaging/rpm/mcabber-ng.spec"
    rpmbuild -bb --define "pkgversion $RPMVERSION" \
      "$SRC/packaging/rpm/mcabber-ng.spec"
    find "$HOME/rpmbuild/RPMS" -name 'mcabber-ng-*.rpm' \
      ! -name '*-debuginfo-*' ! -name '*-debugsource-*' -exec cp {} "$OUT/" \;
    ;;

  arch)
    pacman -Syu --noconfirm --needed base-devel ncurses glib2 loudmouth \
      luajit gpgme libidn enchant autoconf automake libtool pkgconf
    id builder >/dev/null 2>&1 || useradd -m builder
    PKGDIR=/home/builder/pkg
    rm -rf "$PKGDIR"
    mkdir -p "$PKGDIR"
    cp "$SRC/packaging/arch/PKGBUILD" "$PKGDIR/"
    cp -a "$TREE" "$PKGDIR/src-tree"
    chown -R builder "$PKGDIR"
    ARCHVERSION=$(echo "$VERSION" | tr - _)
    su builder -c "cd $PKGDIR && MCNG_PKGVER=$ARCHVERSION makepkg --noconfirm"
    cp "$PKGDIR"/mcabber-ng-"$ARCHVERSION"-*.pkg.tar.zst "$OUT/"
    ;;

  *)
    echo "Unknown package type: $FAMILY" >&2
    exit 1
    ;;
esac

ls -l "$OUT"
