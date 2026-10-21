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
# Without the leading empty lines, and with the lines of each paragraph or
# list item joined: GitHub shows each line break of a release description.
printf '%s\n' "$notes" | sed -e '/./,$!d' | awk '
  /^```/ { if (line != "") print line; line = ""; print; code = !code; next }
  code { print; next }
  /^[ \t]*$/ || /^#/ || /^[ \t]*([-*+]|[0-9]+\.)[ \t]/ {
    if (line != "") print line
    line = ""
    if (/^[ \t]*$/ || /^#/) { print; next }
    line = $0
    next
  }
  {
    sub(/^[ \t]+/, "")
    line = (line == "") ? $0 : line " " $0
  }
  END { if (line != "") print line }
'
