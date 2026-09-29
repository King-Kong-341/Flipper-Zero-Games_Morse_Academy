# Builds Morse Academy with ufbt and copies the .fap to the repository root.
# Usage (PowerShell):  .\scripts\build.ps1
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")
python -m ufbt
if ($LASTEXITCODE -ne 0) { throw "Build failed" }
Copy-Item "dist\morse_academy.fap" "morse_academy.fap" -Force
Write-Host "OK - morse_academy.fap is ready (copy it to SD Card/apps/Games/)."
