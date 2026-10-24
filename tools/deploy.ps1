# Upload the ROM to an EverDrive 64 X7 over USB, boot it, and stream the
# debug log (debugf output) to the console and to logs\usb-<timestamp>.log.
#
# Usage: powershell -ExecutionPolicy Bypass -File tools\deploy.ps1 [rom.z64]
#
# Before running: console ON, sitting in the EverDrive menu, USB cable plugged
# into the cart. Needs UNFLoader.exe (https://github.com/buu342/N64-UNFLoader)
# on PATH, or set $env:UNFLOADER to its full path. Quit with Ctrl+C.
# Flags: -f 3 = EverDrive 3.0/X7, -r = upload ROM, -d FILE = debug log, -b = plain output.
param([string]$Rom = "")

$repo = Split-Path -Parent $PSScriptRoot
if ($Rom -eq "") { $Rom = Join-Path $repo "hivebreakers.z64" }
$unf = if ($env:UNFLOADER) { $env:UNFLOADER } else { "UNFLoader.exe" }

if (-not (Test-Path $Rom)) { Write-Error "ROM not found: $Rom"; exit 1 }
if (-not (Get-Command $unf -ErrorAction SilentlyContinue)) {
    Write-Error "UNFLoader not found. Put UNFLoader.exe on PATH or set `$env:UNFLOADER."
    exit 1
}

$logs = Join-Path $repo "logs"
New-Item -ItemType Directory -Force -Path $logs | Out-Null
$log = Join-Path $logs ("usb-" + (Get-Date -Format "yyyyMMdd-HHmmss") + ".log")
Write-Host "Uploading $Rom to EverDrive X7; USB log -> $log"
& $unf -f 3 -r $Rom -d $log -b
