#! /bin/sh

set -e # exit on error
set -x # Enable echoing of commands

mkdir -p m4
aclocal -I m4
autoheader
automake --foreign --add-missing --force-missing
autoconf
