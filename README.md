# dfu-programmer

dfu-programmer is an implementation of the Device Firmware Upgrade class USB driver that enables firmware upgrades for various USB enabled (with the correct bootloader) Atmel chips.
This program was created because the Atmel "FLIP" program for flashing devices does not support flashing via USB on Linux, and because standard DFU loaders do not work for Atmel's chips.

Check out the Atmel website for more information.
They are kind enough to provide generally correct specifications this implementation is based on.

The project website is http://dfu-programmer.github.io and you can use that to check for updates.

All official builds and releases are on GitHub.

[![Build dfu-programmer](https://github.com/dfu-programmer/dfu-programmer/actions/workflows/build.yml/badge.svg)](https://github.com/dfu-programmer/dfu-programmer/actions/workflows/build.yml)

## Simple install procedure for Unix/Linux/MAC

```bash
tar -xzf dfu-programmer-<version>.tar.gz # unpack the sources
```

_or_

```bash
git clone https://github.com/dfu-programmer/dfu-programmer.git
```

```bash
cd dfu-programmer # change to the top-level directory
```

> If the source was checked-out from GitHub, run the following command.
> You may also need to do this if your libusb is in a non-standard location, or if the build fails to find it for some reason.
> This command requires that `autoconf` is installed (`sudo apt-get install autoconf`).
> 
> ```bash
> ./bootstrap.sh # regenerate base config files
> ```

```bash
./configure # regenerate configure and run it
```

> Optionally you can specify where dfu-programmer gets installed using the `--prefix=` option to the `./configure` command.
> See `./configure --help` for more details.

> If usb library is not available try getting `sudo apt-get install libusb-1.0-0-dev`.

```bash
make # build dfu-programmer
sudo make install # install dfu-programmer
```

> Instructions for installing autocompletion will also be displayed during `make` (or `make bash-completion`).

## Build procedure for Windows

Building Windows apps from source is never quite as simple...
Firstly you need to have MinGW and MSys with developer tools.
Get them from http://sourceforge.net/projects/mingw/files/.
See `.github/workflows/build.yml` for examples of building on Windows and the needed tools.

If you install the correct package with `pacman`, header and lib files will be installed in the correct locations.

- **32-bit:** `pacman -S mingw-w64-i686-libusb`
- **64-bit:** `pacman -S mingw-w64-x86_64-libusb`

Follow the same install instructions as above.

## Windows Driver Files

Windows's built-in WinUSB driver should work out of the box.

### Atmel FLIP

Atmel's [FLIP programmer](https://www.microchip.com/en-us/development-tool/flip) also uses libusb-win32, so we can take advantage of Atmel's official certified drivers.

### Zadig

[Zadig](https://zadig.akeo.ie) is another popular tool for managing the current USB driver for devices on your system.
It can be used to install the libusb-win32, libusbK, WinUSB, or "USB Serial (CDC)" drivers.
All but "USB Serial (CDC)" will work with dfu-programmer.

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