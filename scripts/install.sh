#!/usr/bin/env sh
# Builds Morse Academy, installs it on a USB-connected Flipper and starts it.
# Close qFlipper first - it blocks the USB port.
set -e
cd "$(dirname "$0")/.."
python3 -m ufbt launch
