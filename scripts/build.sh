#!/usr/bin/env sh
# Builds Morse Academy with ufbt and copies the .fap to the repository root.
# Usage:  ./scripts/build.sh
set -e
cd "$(dirname "$0")/.."
python3 -m ufbt
cp dist/morse_academy.fap morse_academy.fap
echo "OK - morse_academy.fap is ready (copy it to SD Card/apps/Games/)."
