# Renders every screen of the app to PNG with the Flipper's real fonts.
# Needs Python 3 + Pillow (pip install pillow) and ufbt installed once.
# Usage (PowerShell):  .\scripts\preview.ps1   -> tools\flipper_preview\shots\
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..\tools\flipper_preview")
python sim_screens.py
Write-Host "Screens written to tools\flipper_preview\shots\"
