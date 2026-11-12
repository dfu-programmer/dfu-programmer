#! /bin/sh

set -e # exit on error
set -x # Enable echoing of commands

mkdir -p m4
autoreconf --force --install
