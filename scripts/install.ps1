# Copies the built mod into an Azimuth folder (the one with azimuth.exe).
# Run after build.ps1.
#
#   PS> .\scripts\install.ps1 -AzimuthDir "D:\Games\Azimuth"
#
# Existing files are backed up to *.bak before being overwritten. Your
# config.toml, fh2.ini, Spotify login and downloaded helpers are kept.

param(
    [Parameter(Mandatory = $true)] [string] $AzimuthDir
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$dist = Join-Path $root "dist"

if (-not (Test-Path (Join-Path $dist "d3dcompiler_47.dll"))) {
    throw "dist\d3dcompiler_47.dll not found -- run scripts\build.ps1 first."
}
if (-not (Test-Path $AzimuthDir)) {
    throw "Azimuth directory not found: $AzimuthDir"
}
if (-not (Test-Path (Join-Path $AzimuthDir "azimuth.exe"))) {
    Write-Warning "azimuth.exe not found in $AzimuthDir -- make sure this is the right folder."
}

function Backup-AndCopy($src, $dst) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dst) | Out-Null
    if (Test-Path $dst) { Copy-Item -Force $dst "$dst.bak" }
    Copy-Item -Force $src $dst
    "  + $($dst.Substring($AzimuthDir.Length).TrimStart('\'))"
}

Backup-AndCopy (Join-Path $dist "d3dcompiler_47.dll") (Join-Path $AzimuthDir "d3dcompiler_47.dll") | Out-Host

$dataDir = Join-Path $AzimuthDir "fh2-radio"
New-Item -ItemType Directory -Force -Path $dataDir | Out-Null
Copy-Item -Recurse -Force (Join-Path $dist "fh2-radio\ui") $dataDir
Copy-Item -Recurse -Force (Join-Path $dist "fh2-radio\assets") $dataDir
Copy-Item -Force (Join-Path $dist "README.txt") (Join-Path $dataDir "README.txt")
$cfg = Join-Path $dataDir "config.toml"
if (-not (Test-Path $cfg)) {
    Copy-Item (Join-Path $dist "fh2-radio\config.toml") $cfg
    Write-Host "  + fh2-radio\config.toml  (seeded from example)" -ForegroundColor Yellow
}
Backup-AndCopy (Join-Path $dist "fh2-radio\fh2-radio-worker.exe") (Join-Path $dataDir "fh2-radio-worker.exe") | Out-Host

Write-Host "`nDone. Launch FH2, set Audio > Radio DJ = Off and tune to Horizon Pulse." -ForegroundColor Green
Write-Host "Then open http://localhost:8420 in any browser on this machine."
