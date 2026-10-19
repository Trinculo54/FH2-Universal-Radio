# One-shot Windows build. Configures CMake with the llvm-mingw toolchain
# (Clang is needed for the MSVC-style SEH the code uses), compiles, then
# stages the mod's package (an FH2ML v4 mod) in dist\.
#
#   PS> .\scripts\build.ps1
#
# Needs llvm-mingw (https://github.com/mstorsjo/llvm-mingw/releases, the
# "ucrt-x86_64" zip) with its bin\ folder on PATH, plus CMake and Ninja.
#
# Output: dist\mods\universalradio\, so a zip of dist\ extracts straight into
# the Azimuth folder (or goes to the FH2ML Manager's Install mod):
#   mod.json                FH2ML's manifest, with the build's version
#   FH2UniversalRadio.dll   the radio
#   fh2-radio-worker.exe    starts the helper programs outside the game
#   ui\                     dashboard (served at http://localhost:<port>)
#   assets\                 default artwork
#   config.example.toml     copied to FH2ML\data\universalradio\config.toml
#                           on the first launch
#   README.txt, LICENSE.txt

$ErrorActionPreference = "Stop"
$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root "build"
$dist  = Join-Path $root "dist"

foreach ($tool in @("cmake", "ninja", "x86_64-w64-mingw32-clang++")) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "$tool not found on PATH. Install CMake, Ninja and llvm-mingw (add its bin\ folder to PATH)."
    }
}

if (-not (Test-Path (Join-Path $root "third_party\nlohmann\nlohmann\json.hpp"))) {
    Write-Host "third_party/ has no downloaded headers yet -- running get-deps.ps1 first." -ForegroundColor Yellow
    & (Join-Path $PSScriptRoot "get-deps.ps1")
}

Write-Host "-> cmake configure" -ForegroundColor Cyan
& cmake -S $root -B $build -G Ninja `
    "-DCMAKE_TOOLCHAIN_FILE=$(Join-Path $root 'cmake\mingw-w64-x86_64.cmake')" `
    -DCMAKE_BUILD_TYPE=Release | Out-Host
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

Write-Host "-> cmake build (Release)" -ForegroundColor Cyan
& cmake --build $build | Out-Host
if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }

$pkg = Join-Path $dist "mods\universalradio"

if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
New-Item -ItemType Directory -Force -Path (Join-Path $pkg "assets") | Out-Null

Copy-Item (Join-Path $build "mod.json") $pkg
Copy-Item (Join-Path $build "FH2UniversalRadio.dll") $pkg
Copy-Item (Join-Path $build "fh2-radio-worker.exe") $pkg
Copy-Item -Recurse (Join-Path $root "ui\dist") (Join-Path $pkg "ui")
Copy-Item (Join-Path $root "assets\default_artwork.png") (Join-Path $pkg "assets\default_artwork.png")
Copy-Item (Join-Path $root "config.example.toml") $pkg
Copy-Item (Join-Path $PSScriptRoot "dist-readme.txt") (Join-Path $pkg "README.txt")
Copy-Item (Join-Path $root "LICENSE") (Join-Path $pkg "LICENSE.txt")

Write-Host "`nBuilt + staged in $dist" -ForegroundColor Green
Get-ChildItem -Recurse -File $dist | ForEach-Object {
    "  $($_.FullName.Substring($dist.Length + 1))"
}
