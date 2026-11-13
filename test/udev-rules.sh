#!/bin/sh
# make check: udev/70-dfu-programmer.rules must match the target table.
# Fix a failure with `make update-udev-rules`.
bin=./src/dfu-programmer
"$bin" --version >/dev/null 2>&1 || exit 77   # can't run it (cross build)

"$bin" --targets-udev | tr -d '\r' > udev-rules.tmp || exit 99
tr -d '\r' < "$srcdir/udev/70-dfu-programmer.rules" > udev-rules.want
if cmp -s udev-rules.want udev-rules.tmp; then
    exit 0
fi
diff udev-rules.want udev-rules.tmp
echo "udev/70-dfu-programmer.rules is out of date: run 'make update-udev-rules'" >&2
exit 1
