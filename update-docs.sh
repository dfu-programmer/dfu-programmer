#!/bin/sh
# Regenerate the supported-chips lists in the docs from the target table.
#
# usage: update-docs.sh BIN SRCDIR OUTDIR
#
# Reads README.md, docs/dfu-programmer.1 and docs/dfu-programmer.html from
# SRCDIR, replaces the lines between their "BEGIN TARGETS" and "END TARGETS"
# markers with the output of `BIN --targets-markdown`, `--targets-tex` and
# `--targets-html`, and writes the results under OUTDIR (which may be SRCDIR).
set -e

BIN=$1
SRCDIR=$2
OUTDIR=$3
if [ -z "$BIN" ] || [ -z "$SRCDIR" ] || [ -z "$OUTDIR" ]; then
    echo "usage: $0 BIN SRCDIR OUTDIR" >&2
    exit 2
fi

# replace FILE LIST-OPTION
replace() {
    mkdir -p "$OUTDIR/$(dirname "$1")"
    "$BIN" "$2" | tr -d '\r' > "$OUTDIR/$1.list"
    tr -d '\r' < "$SRCDIR/$1" | awk -v list="$OUTDIR/$1.list" '
        /BEGIN TARGETS/ {
            print
            while ((getline line < list) > 0) print line
            skipping = 1
            found = 1
            next
        }
        /END TARGETS/ { skipping = 0 }
        !skipping { print }
        END { if (!found) exit 1 }
    ' > "$OUTDIR/$1.new" || { echo "$0: no BEGIN TARGETS marker in $1" >&2; exit 1; }
    mv "$OUTDIR/$1.new" "$OUTDIR/$1"
    rm -f "$OUTDIR/$1.list"
}

replace README.md --targets-markdown
replace docs/dfu-programmer.1 --targets-tex
replace docs/dfu-programmer.html --targets-html
