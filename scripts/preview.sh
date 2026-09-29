#!/usr/bin/env sh
# Renders every screen of the app to PNG with the Flipper's real fonts.
# Needs Python 3 + Pillow (pip install pillow) and ufbt installed once.
set -e
cd "$(dirname "$0")/../tools/flipper_preview"
python3 sim_screens.py
echo "Screens written to tools/flipper_preview/shots/"
