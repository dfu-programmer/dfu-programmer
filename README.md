# dfu-programmer

dfu-programmer is an implementation of the Device Firmware Upgrade class USB driver that enables firmware upgrades for various USB enabled (with the correct bootloader) Atmel chips.
This program was created because the Atmel "FLIP" program for flashing devices does not support flashing via USB on Linux, and because standard DFU loaders do not work for Atmel's chips.

Check out the Atmel website for more information.
They are kind enough to provide generally correct specifications this implementation is based on.

The project website is http://dfu-programmer.github.io and you can use that to check for updates.

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
> This command requires that autoconf is installed (`sudo apt-get install autoconf`).

```bash
./bootstrap.sh # regenerate base config files
```

> Optionally you can add autocompletion using the dfu_completion file, and possibly instructions provided after running the ./bootstrap command

```bash
./configure # regenerate configure and run it
```

> Optionally you can specify where dfu-programmer gets installed using the `--prefix=` option to the `./configure` command.
> See `./configure --help` for more details.

> By default the build process will use libusb-1.0 if available.
> It tries to auto-discover the library, falling back to the older libusb if libusb-1.0 is not available.
> This process is not entirely reliable and may decide that libusb-1.0 is available when in fact it is not.
> You can select libusb using `--disable-libusb_1_0`.
> If usb library is not available try getting `libusb-1.0-0-dev`.

```bash
make # build dfu-programmer
sudo make install # install dfu-programmer
```

## Build procedure for Windows

Building Windows apps from source is never quite as simple...
Firstly you need to have MinGW and MSys with developer tools.
Get them from http://sourceforge.net/projects/mingw/files/.
See `.github/workflows/build.yml` for examples of building on Windows and the needed tools.

Open the MinGW shell window and change to the dfu-programmer folder.
_Note that `C:\dir` is accessed in MinGW using the path `/c/dir`._

> If the source was checked-out from GitHub, run:
>
> ```bash
> ./bootstrap.sh
> ```

### libusb1

The Windows build now supports libusb-1.0.
If you install the correct package with `pacman`, include and lib files will be installed in the correct locations.

- **32-bit:** `pacman -S mingw-w64-i686-libusb`
- **64-bit:** `pacman -S mingw-w64-x86_64-libusb`

```bash
./configure
```

### libusb0

The Windows build supports the libusb-win32 library, which is a port of libusb 0.1.
For convenience these files are included with this distribution, located in the windows sub-directory.
You need to copy these files to your MinGW install directory if they are not already there:

- `windows/usb.h` -> `{path-to-mingw}/include/usb.h`
- `windows/libusb.a` -> `{path-to-mingw}/lib/libusb.a`

```bash
./configure --disable-libusb_1_0
```

### Build

```bash
make
```

The executable will be built in the `dfu-programmer/src/` folder.

## Windows Driver Files

### Atmel FLIP

Atmel's FLIP programmer also uses libusb-win32, so we can take advantage of Atmel's official certified drivers.
A copy is included in the libusb0 versions of the Windows distributions (`dfu-programmer-win-*`).

### Zadig

[Zadig](https://zadig.akeo.ie) is another popular tool for managing the current USB driver for devices on your system.
It can be used to install the libusb-win32, libusbK, WinUSB, or "USB Serial (CDC)" drivers.
The libusb-win32 driver has the broadest compatibility with builds of dfu-programmer, but that is not relevant on libusb1 builds of dfu-programmer.

| Zadig Driver     | libusb0 x86 | libusb1 x86 | libusb1 x64 |
| ---------------- | ----------- | ----------- | ----------- |
| WinUSB           | ❌          | ✅          | ✅          |
| libusbK          | ✅          | ✅          | ✅          |
| libusb-win32     | ✅          | ✅          | ✅          |
| USB Serial (CDC) | ❌          | ❌          | ❌          |

### USB on Windows Simplified

![USB on Windows Simplified](docs/USB%20on%20Windows.drawio.svg "USB Drivers on Windows")
