# Contributing to dfu-programmer

Bug reports, fixes and new targets are welcome.
Open an [issue](https://github.com/dfu-programmer/dfu-programmer/issues) or a pull request.

## Reporting a bug

Include:

- the dfu-programmer version (`dfu-programmer --version`) and your OS;
- the exact command and its full output, ideally with `--debug 50` added;
- the chip and how it entered bootloader mode.

## Building and testing

See [Build from source](README.md#build-from-source) in the README.
From a git checkout:

```bash
./bootstrap.sh
./configure --enable-werror
make
make check
```

`make check` runs the hardware-free tests: unit tests in `test/unit/` and a check that `udev/70-dfu-programmer.rules` matches the target table.
`make distcheck` also builds and tests the release tarball out of tree, the way packagers do.

The TypeScript tests in `test/` drive the real binary. Run the hardware-free ones with:

```bash
npm ci
npm run test:standalone
```

The rest need a real chip and run on the AVR Test HAT CI runner. See [test/README.md](test/README.md).

## Code

- The code is C, built with `-Wall -Wextra`. CI builds with `--enable-werror`, so new warnings fail the build.
- CI builds on Linux (x64, arm64, gcc, clang, ASan + UBSan), macOS, Windows (MSYS2) and Fedora (RPM). Keep it portable: no compiler extensions, and use the `PRI*` macros from `<inttypes.h>` to print fixed-width integers.
- Add a unit test under `test/unit/` for anything that can be tested without hardware.

## Adding a target

1. Add the chip to `target_map` in `src/arguments.c` and to `targets_enum` in `src/arguments.h`.
2. Run `make update-udev-rules` to regenerate `udev/70-dfu-programmer.rules`.
3. Add it to the supported chips list in `README.md` and in `docs/dfu-programmer.1` and `docs/dfu-programmer.html` (`--targets-tex` and `--targets-html` print those lists).

## Pull requests

- Keep each PR to one change, with a commit message that says why.
- CI must pass. The AVR Test HAT job only runs when its runner is online; a pending HAT job doesn't block review.
