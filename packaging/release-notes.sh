#!/bin/sh
# Print the section of CHANGELOG.md for a version (without its title):
#
#   packaging/release-notes.sh VERSION [CHANGELOG]
#
# Exits with status 1 if there is no section for this version.

set -eu

VERSION=$1
CHANGELOG=${2:-CHANGELOG.md}

notes=$(awk -v version="$VERSION" '
  /^## / {
    if (found) exit
    if ($2 == version) { found = 1; next }
  }
  found { print }
  END { exit found ? 0 : 1 }
' "$CHANGELOG") || exit 1
# Without the leading empty lines
printf '%s\n' "$notes" | sed -e '/./,$!d'
