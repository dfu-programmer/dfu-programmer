#!/bin/sh
# make check: the supported-chips lists in README.md and the man page must
# match the target table. Fix a failure with `make update-docs`.
bin=./src/dfu-programmer
"$bin" --version >/dev/null 2>&1 || exit 77   # can't run it (cross build)

out=docs-targets.tmp
mkdir -p "$out"
sh "$srcdir/update-docs.sh" "$bin" "$srcdir" "$out" || exit 99

status=0
for f in README.md docs/dfu-programmer.1 docs/dfu-programmer.html; do
    if ! tr -d '\r' < "$srcdir/$f" | cmp -s - "$out/$f"; then
        tr -d '\r' < "$srcdir/$f" | diff - "$out/$f"
        echo "$f: supported chips list is out of date: run 'make update-docs'" >&2
        status=1
    fi
done
exit $status
