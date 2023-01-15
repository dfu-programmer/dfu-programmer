# Tests

These tests should be able to run on any system, given that hardware requirements are met.
Our local unit test are run on an [AVR Test HAT](https://github.com/cinderblock/avr-test-hat).

### Minimum "Full" Test System Setup

- ATmega8u2 connected to the USB port of the test system.
- AVRDUDE setup to program the ATmega8u2
  - A program on the path called `avrdude.sh` that adds the correct `-c`, `-B`, `-P`, and `-p` options to the `avrdude` command. Running without arguments should successfully communicate and identify the ATmega8u2.
- Permissions to access usb devices as the current user.

## Goals

1. ✅ Functionality testing
2. 💯% code coverage (This will take more hardware!)

## Tests

We're using [Jest](https://jestjs.io) to run the tests with a thin wrapper around the executable.

To run these tests on your system, you just need a recent-ish version of [Node.js](https://nodejs.org) installed.

Then run:

```bash
npm install
npm test
```

### Software Environment

Available environment variables:

 - `$DFU` - The path to the recently built `dfu-programmer` executable.
 - `$TARGET` - The correct "target" argument that dfu-programmer should use for the attached device.
 - `$AVRDUDE` - A path to avrdude to needed flags. Just add "-U flash:r:-:h" to read the flash memory as hex bytes

## Setup Script for new Pi

```bash
# Dependencies
:|sudo apt update
curl -fsSL https://deb.nodesource.com/setup_18.x | sudo -E bash -
:|sudo apt install -y git automake build-essential libusb-1.0-0-dev python3-pip nodejs
sudo pip install cpp-coveralls

# Runner user
sudo useradd -mN -g users -G plugdev,gpio action
sudo -iu action

mkdir -p ~/runner
cd ~/runner
curl -sL https://github.com/actions/runner/releases/download/v2.300.2/actions-runner-linux-arm64-2.300.2.tar.gz | tar xz
./config.sh --url https://github.com/dfu-programmer/dfu-programmer --token <token> # Get token from GitHub Action Self-Hosted Runner page
exit

# Setup Systemd service
cat << EOF | sudo tee /etc/systemd/system/actions-runner.service > /dev/null
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

[Install]
WantedBy=multi-user.target
EOF
sudo systemctl daemon-reload
sudo systemctl enable --now actions-runner
```