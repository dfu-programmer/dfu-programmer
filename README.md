# dfu-programmer

dfu-programmer is an implementation of the Device Firmware Upgrade class USB driver that enables firmware upgrades for various USB enabled (with the correct bootloader) Atmel chips.
This program was created because the Atmel "FLIP" program for flashing devices does not support flashing via USB on Linux, and because standard DFU loaders do not work for Atmel's chips.

Check out the Atmel website for more information.
They are kind enough to provide generally correct specifications this implementation is based on.

The project website is http://dfu-programmer.github.io and you can use that to check for updates.

All official [builds](https://github.com/dfu-programmer/dfu-programmer/actions) and [releases](https://github.com/dfu-programmer/dfu-programmer/releases) are on [GitHub](https://github.com/dfu-programmer/dfu-programmer).

[![Build dfu-programmer](https://github.com/dfu-programmer/dfu-programmer/actions/workflows/build.yml/badge.svg)](https://github.com/dfu-programmer/dfu-programmer/actions/workflows/build.yml)
[![Coverage Status](https://coveralls.io/repos/github/dfu-programmer/dfu-programmer/badge.svg?branch=master)](https://coveralls.io/github/dfu-programmer/dfu-programmer?branch=master)

## Install

Releases are on the [releases page](https://github.com/dfu-programmer/dfu-programmer/releases).
Packaged versions:

| Platform | Command |
| --- | --- |
| macOS ([Homebrew](https://formulae.brew.sh/formula/dfu-programmer)) | `brew install dfu-programmer` |
| macOS ([MacPorts](https://ports.macports.org/port/dfu-programmer/)) | `sudo port install dfu-programmer` |
| Debian 14+, Ubuntu 26.04+ | `sudo apt install dfu-programmer` |
| Arch Linux | `sudo pacman -S dfu-programmer` |
| Windows ([Scoop](https://scoop.sh), extras bucket) | `scoop bucket add extras` then `scoop install dfu-programmer` |
| Windows ([MSYS2](https://packages.msys2.org/base/mingw-w64-dfu-programmer), UCRT64 shell) | `pacman -S mingw-w64-ucrt-x86_64-dfu-programmer` |

Older Debian and Ubuntu releases ship 0.6.1, which is missing many targets and fixes; build from source or use a release binary instead.
On Windows you can also download the release `.zip`, unpack it anywhere, and run `dfu-programmer.exe` from a terminal.

On Linux, see [Running without root](#running-without-root-on-linux). On Windows, see [Windows drivers](#windows-drivers).

## Quick start

Put the chip in its bootloader (DFU) mode, then:

```bash
dfu-programmer atmega32u4 erase
dfu-programmer atmega32u4 flash firmware.hex
dfu-programmer atmega32u4 launch
```

`dfu-programmer --targets` lists every target name, and `dfu-programmer --help` lists every command and option.
The [man page](docs/dfu-programmer.html) has the details.

## Windows drivers

dfu-programmer uses [libusb](https://libusb.info), which needs a driver bound to the bootloader.
Atmel's bootloaders don't tell Windows which driver to use, so set one up once per bootloader:

1. Put the chip in bootloader mode and plug it in.
2. Run [Zadig](https://zadig.akeo.ie). Choose **Options → List All Devices** if the bootloader isn't listed.
3. Select the bootloader, choose **WinUSB** as the driver, and click **Install Driver** (or **Replace Driver**).

**WinUSB** is the recommended driver, and the one we test with.
**libusbK** also works.

Atmel FLIP installs its own **libusb-win32** driver (Zadig calls it **libusb0**).
libusb-1.0 doesn't fully support that driver, so dfu-programmer may not see the device, or may misbehave.
Replace it with WinUSB using Zadig.
FLIP stops seeing the device until you switch back.

### AT90USB64/128 not recognized on Windows 8 and later

On Windows 8 and later, some AT90USB64 and AT90USB128 bootloaders don't enumerate: Device Manager shows a ⚠️ icon and dfu-programmer reports `no device present`.
These bootloaders claim to be USB 20.00 (instead of 2.00), so Windows asks them for a descriptor they don't have.
Microsoft's [explanation](https://techcommunity.microsoft.com/t5/microsoft-usb-blog/why-does-my-usb-device-work-on-windows-8-0-but-fail-on-windows-8/ba-p/270819) has the background.
To have Windows skip that request for the bootloader, run this in an **administrator** Command Prompt, then replug the device:

```bat
:: AT90USB64x (USB ID 03EB:2FF9)
set K=HKLM\SYSTEM\CurrentControlSet\Control\usbflags\03EB2FF90000
:: AT90USB128x: use 03EB2FFB0000 instead
reg add %K% /v osvc /t REG_BINARY /d 0000 /f
reg add %K% /v SkipContainerIdQuery /t REG_BINARY /d 01000000 /f
reg add %K% /v SkipBOSDescriptorQuery /t REG_BINARY /d 01000000 /f
```

The last four digits of the key are the bootloader's USB device release number.
If yours isn't `0000`, Device Manager shows it as `REV_xxxx` under **Details → Hardware Ids**.
The fix and the root cause were found in [#86](https://github.com/dfu-programmer/dfu-programmer/issues/86).

## Common problems

**`no device present`**
- Check the chip is in bootloader mode: it enumerates with a different USB ID than your firmware.
- Check the target name: `dfu-programmer --targets` lists them, and each one matches one USB ID.
- With more than one bootloader attached, pick one with `target:bus,address`.
- On Linux, see [Running without root](#running-without-root-on-linux). On Windows, see [Windows drivers](#windows-drivers).

**`LIBUSB_ERROR_ACCESS`** (Linux): you don't have permission to open the device. See [Running without root](#running-without-root-on-linux).

**`Device is write protected.`**
The chip's lock bits (or security bit) are set, and the bootloader refuses to write until the chip is erased.
Run `erase` first, or use `flash --erase-first`.

**Clearing the AVR32 user page**
`erase` doesn't touch the AVR32 user page, because its last word holds the bootloader's configuration (including the pin that selects bootloader mode).
To change other bytes, back the page up, edit it, and write it back:

```bash
dfu-programmer at32uc3b0256 read --user > user.hex
cp user.hex user-backup.hex
# edit user.hex, keeping the last 4 bytes and fixing each line's checksum
dfu-programmer at32uc3b0256 flash --user --force user.hex
```

Writing the user page doesn't need an erase first.
See [#30](https://github.com/dfu-programmer/dfu-programmer/issues/30).

## Build from source

### Linux, macOS and other Unix

Install a C compiler, `make`, `pkg-config`, and libusb 1.0.22 or newer with its headers (`libusb-1.0-0-dev` on Debian and Ubuntu, `libusb1-devel` on Fedora, `libusb` on Homebrew).
Then build from a release tarball:

```bash
tar -xzf dfu-programmer-<version>.tar.gz
cd dfu-programmer-<version>
./configure
make
make check         # optional: hardware-free tests
sudo make install
```

From a git checkout, run `./bootstrap.sh` before `./configure`. It needs `autoconf` and `automake`.

`./configure --help` lists the options, such as `--prefix=DIR`.
`make` also prints how to install bash completion (or run `make bash-completion`).

On Fedora, `rpmbuild -ta dfu-programmer-<version>.tar.gz` builds an RPM from the release tarball.

### Running without root on Linux

`make install` also installs `70-dfu-programmer.rules` into udev's rules directory (found with `pkg-config udev`; choose another with `./configure --with-udevrulesdir=DIR`, or skip it with `--without-udevrulesdir`).
The rules give the logged-in user access to every supported bootloader.
Load them with `sudo udevadm control --reload-rules`, then replug the device.

If you didn't install from source, newer versions print the same rules with `dfu-programmer --targets-udev`:

```bash
dfu-programmer --targets-udev | sudo tee /etc/udev/rules.d/70-dfu-programmer.rules
sudo udevadm control --reload-rules
```

### Windows

Install [MSYS2](https://www.msys2.org) and open its **UCRT64** shell (or **CLANGARM64** on Arm).
Install the tools and libusb:

```sh
pacman -S autoconf-wrapper automake-wrapper make mingw-w64-ucrt-x86_64-cc mingw-w64-ucrt-x86_64-libusb mingw-w64-ucrt-x86_64-pkgconf
```

(On CLANGARM64, use the `mingw-w64-clang-aarch64-` prefix instead.)
Then follow the Unix steps above.
`.github/workflows/build.yml` shows the exact steps CI uses.

## Testing & Coverage

Since most testing depends on hardware, we've set up a custom GitHub Action Runner on a dedicated Raspberry Pi.
Read more about the tests [here](test/README.md).

## Currently Supported Chips

<details><summary>8051 based controllers</summary>

- at89c51snd1c
- at89c51snd2c
- at89c5130
- at89c5131
- at89c5132

</details>

<details><summary>AVR based controllers</summary>

- at90usb1287
- at90usb1286
- at90usb1287-4k
- at90usb1286-4k
- at90usb647
- at90usb646
- at90usb162
- at90usb82
- atmega32u6
- atmega32u4
- atmega32u2
- atmega16u4
- atmega16u2
- atmega8u2

</details>

<details><summary>AVR32 based controllers</summary>

- at32uc3a0128
- at32uc3a1128
- at32uc3a0256
- at32uc3a1256
- at32uc3a0512
- at32uc3a1512
- at32uc3a0512es
- at32uc3a1512es
- at32uc3a364
- at32uc3a364s
- at32uc3a3128
- at32uc3a3128s
- at32uc3a3256
- at32uc3a3256s
- at32uc3a4256s
- at32uc3b064
- at32uc3b164
- at32uc3b0128
- at32uc3b1128
- at32uc3b0256
- at32uc3b1256
- at32uc3b0256es
- at32uc3b1256es
- at32uc3b0512
- at32uc3b1512
- at32uc3c064
- at32uc3c0128
- at32uc3c0256
- at32uc3c0512
- at32uc3c164
- at32uc3c1128
- at32uc3c1256
- at32uc3c1512
- at32uc3c264
- at32uc3c2128
- at32uc3c2256
- at32uc3c2512

</details>

<details><summary>XMEGA based controllers</summary>

- atxmega64a1u
- atxmega128a1u
- atxmega64a3u
- atxmega128a3u
- atxmega192a3u
- atxmega256a3u
- atxmega16a4u
- atxmega32a4u
- atxmega64a4u
- atxmega128a4u
- atxmega256a3bu
- atxmega64b1
- atxmega128b1
- atxmega64b3
- atxmega128b3
- atxmega64c3
- atxmega128c3
- atxmega256c3
- atxmega384c3
- atxmega16c4
- atxmega32c4

</details>

<details><summary>Experimental support for ST cortex M4</summary>

- stm32f4_B
- stm32f4_C
- stm32f4_E
- stm32f4_G

</details>