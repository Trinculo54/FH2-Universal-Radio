# Copies the built mod into an Azimuth folder (the one with azimuth.exe), as
# mods\universalradio, where FH2ML loads it. Run after build.ps1.
#
#   PS> .\scripts\install.ps1 -AzimuthDir "D:\Games\Azimuth"
#
# The mod's folder is replaced, the way an update through the FH2ML Manager
# replaces it. Your fh2.ini, config.toml, Spotify login and downloaded helpers
# live in FH2ML\data\universalradio and aren't touched.

param(
    [Parameter(Mandatory = $true)] [string] $AzimuthDir
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$pkg  = Join-Path $root "dist\mods\universalradio"

if (-not (Test-Path (Join-Path $pkg "mod.json"))) {
    throw "dist\mods\universalradio not found -- run scripts\build.ps1 first."
}
if (-not (Test-Path $AzimuthDir)) {
    throw "Azimuth directory not found: $AzimuthDir"
}
if (-not (Test-Path (Join-Path $AzimuthDir "azimuth.exe"))) {
    Write-Warning "azimuth.exe not found in $AzimuthDir -- make sure this is the right folder."
}
if (-not (Test-Path (Join-Path $AzimuthDir "data\bin\FH2ML.Runtime.dll"))) {
    Write-Warning "FH2ML isn't installed in $AzimuthDir yet; the radio needs it (https://github.com/Refilewski/FH2ML)."
}

$mods = Join-Path $AzimuthDir "mods"
$dst  = Join-Path $mods "universalradio"
if (Test-Path $dst) { Remove-Item -Recurse -Force $dst }
New-Item -ItemType Directory -Force -Path $mods | Out-Null
Copy-Item -Recurse $pkg $dst
Get-ChildItem -Recurse -File $dst | ForEach-Object {
    "  + mods\universalradio\$($_.FullName.Substring($dst.Length + 1))"
}

Write-Host "`nDone. Launch FH2, set Audio > Radio DJ = Off and tune to Horizon Pulse." -ForegroundColor Green
Write-Host "Then open http://localhost:8420 in any browser on this machine."
