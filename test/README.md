# Tests

These tests should be able to run on any system, given that hardware requirements are met.
Our local unit test are run on an [AVR Test HAT](https://github.com/cinderblock/avr-test-hat).

### Minimum "Full" Test System Setup

- ATmega8u2 connected to the USB port of the test system.
- Permissions to access usb devices as the current user.
- AVRDUDE setup to program the ATmega8u2
  - A program on the path called `avrdude.sh` that adds the correct `-c`, `-B`, `-P`, and `-p` options to the `avrdude` command. Running without arguments should successfully communicate and identify the ATmega8u2.

## Goals

1. ✅ Functionality testing
2. 💯% code coverage (This will take **much more** hardware!)

## Tests

We're using [Jest](https://jestjs.io) to run the tests with a thin wrapper around the executable.

To run these tests on your system, you just need a recent-ish version of [Node.js](https://nodejs.org) installed.

Then run:

```bash
npm install
npm test
```

### Standalone Tests<!-- Happy to change the name of these or how tests are filtered -->

Some tests expect a compatible `usb` device to be attached.

To run only the standalone tests, use:

```bash
npm test -- standalone
```

### Software Environment

Available environment variables:

 - `$TARGET` - The correct "target" argument that dfu-programmer should use for the attached device.
 - `$AVRDUDE` - A path to avrdude with needed flags. Just add "-U flash:r:-:h" to read the flash memory as hex bytes

## Setup Script for new Pi

You should be able to copy and paste this block into a new Pi install to get it ready to run the tests.

```bash
# Dependencies
:|sudo apt update
curl -fsSL https://deb.nodesource.com/setup_18.x | sudo -E bash -
:|sudo apt install -y git automake build-essential libusb-1.0-0-dev python3-pip nodejs
sudo pip install cpp-coveralls

# AVRDUDE
:|sudo apt -y install cmake flex bison libelf-dev libusb-dev libhidapi-dev libftdi1-dev libreadline-dev
git clone https://github.com/avrdudes/avrdude.git
cd avrdude
./build.sh
sudo cmake --build build_linux --target install

#echo 'SUBSYSTEMS=="usb", ATTRS{idVendor}=="03eb", ATTRS{idProduct}=="2ff4", MODE="0666"' | sudo tee /etc/udev/rules.d/50-avrdude.rules > /dev/null
#sudo udevadm control --reload-rules && sudo udevadm trigger

mkdir -p /home/pi/.config/avrdude
cat << AVRDUDE_RC > /home/pi/.config/avrdude/avrdude.rc
programmer parent "linuxspi"
    id       = "avr-test-hat";
    desc     = "linuxspi on the AVR Test HAT" ;
    # baudrate = <num> ;                        # baudrate for avr910-programmer
    # vcc      = <pin1> [, <pin2> ... ] ;       # pin number(s)
    # buff     = <pin1> [, <pin2> ... ] ;       # pin number(s)
    reset    = 13 ;                        # pin number
    # errled   = <pin> ;                        # pin number
    # rdyled   = <pin> ;                        # pin number
    # pgmled   = <pin> ;                        # pin number
    # vfyled   = <pin> ;                        # pin number
;
AVRDUDE_RC

# AVRDUDE wrapper
cat << 'AVRDUDE_WRAPPER' > /home/pi/avrdude.sh && chmod +x /home/pi/avrdude.sh
#!/bin/bash

set -e

#AVRDUDE="/home/pi/avrdude/build_linux/src/avrdude"
AVRDUDE="/usr/local/bin/avrdude"

part="atmega8u2"
programmer="avr-test-hat"

args=("-c" ${programmer} "-p" ${part})

# Override the default config file
# args+=("-C" "/home/pi/avrdude.conf")

# Fastest working bit clock
# 0.5 - Works usually
# 0.7
args+=("-B" "1")

# TODO: Setup GPIO

function cleanup {
  # TODO: Cleanup GPIO
}
trap cleanup EXIT

$AVRDUDE "${args[@]}" "$@"
AVRDUDE_WRAPPER

# Action Pre/Post Scripts
mkdir -p /home/pi/action-scripts
cat << ACTION_BEFORE > /home/pi/action-scripts/before.sh
# TODO: Reset flash?
ACTION_BEFORE

cat << ACTION_AFTER > /home/pi/action-scripts/after.sh
# TODO: Turn off test hardware?
ACTION_AFTER

# Runner user
sudo useradd -mN -g users -G plugdev,gpio action
sudo ln -s /home/pi/.config /home/action/.config
sudo -u action mkdir -p /home/action/runner
cd /home/action/runner
curl -sL $(curl -s https://api.github.com/repos/actions/runner/releases/latest | grep '"browser_download_url":' | cut -d\" -f4 | egrep 'linux-arm64-[0-9.]+tar.gz$') | sudo -u action tar -xz
sudo -u action ./config.sh --disableupdate --url https://github.com/dfu-programmer/dfu-programmer --token <token> # Get token from GitHub Action Self-Hosted Runner page
cd

# Setup Systemd service
cat << ACTION_SERVICE | sudo tee /etc/systemd/system/actions-runner.service > /dev/null && sudo systemctl daemon-reload
[Unit]
Description=GitHub Actions Runner
After=network.target

[Service]
Type=simple
User=action
WorkingDirectory=/home/action/runner
ExecStart=/home/action/runner/run.sh
Restart=always
RestartSec=5

Environment=ACTIONS_RUNNER_HOOK_JOB_STARTED=/home/pi/action-scripts/before.sh
Environment=ACTIONS_RUNNER_HOOK_JOB_COMPLETED=/home/pi/action-scripts/after.sh

Environment=AVRDUDE=/home/pi/avrdude.sh
Environment=TARGET=atmega8u2

[Install]
WantedBy=multi-user.target
ACTION_SERVICE
sudo systemctl enable --now actions-runner

# Disable unneeded services
sudo systemctl disable --now cron.service
sudo systemctl disable --now triggerhappy.service
sudo systemctl disable --now bluetooth.service
sudo systemctl disable --now ModemManager.service
sudo systemctl disable --now getty@tty1.service

# 0 = enabled, 1 = disabled
sudo raspi-config nonint do_spi 0
sudo raspi-config nonint do_overlayfs 0
```

*The `:|` is a trick to make `apt` not hog all of the pasted code.*