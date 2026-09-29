# Builds Morse Academy, installs it on a USB-connected Flipper and starts it.
# Close qFlipper first - it blocks the USB port.
# Usage (PowerShell):  .\scripts\install.ps1
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")
python -m ufbt launch
if ($LASTEXITCODE -ne 0) { throw "Install failed - is the Flipper connected and qFlipper closed?" }
