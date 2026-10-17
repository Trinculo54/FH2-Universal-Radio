# One-shot Windows build. Configures CMake with the llvm-mingw toolchain
# (Clang is needed for the MSVC-style SEH the code uses), compiles, then
# stages everything that needs to ship in dist\.
#
#   PS> .\scripts\build.ps1
#
# Needs llvm-mingw (https://github.com/mstorsjo/llvm-mingw/releases, the
# "ucrt-x86_64" zip) with its bin\ folder on PATH, plus CMake and Ninja.
#
# Output:
#   dist\d3dcompiler_47.dll     the FH2 Mod Loader (drops next to azimuth.exe)
#   dist\fh2-mods\FH2UniversalRadio.dll  the radio mod
#   dist\fh2-radio\ui\          dashboard (served at http://localhost:<port>)
#   dist\fh2-radio\config.toml  seeded from config.example.toml
#   dist\README.txt             end-user readme

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
    Write-Host "third_party/ is empty -- running get-deps.ps1 first." -ForegroundColor Yellow
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

$dataDir = Join-Path $dist "fh2-radio"

if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
New-Item -ItemType Directory -Force -Path $dataDir | Out-Null

Copy-Item (Join-Path $build "d3dcompiler_47.dll") $dist
New-Item -ItemType Directory -Force -Path (Join-Path $dist "fh2-mods") | Out-Null
Copy-Item (Join-Path $build "FH2UniversalRadio.dll") (Join-Path $dist "fh2-mods")
Copy-Item (Join-Path $build "fh2-radio-worker.exe") $dataDir

New-Item -ItemType Directory -Force -Path (Join-Path $dataDir "assets") | Out-Null
Copy-Item (Join-Path $root "assets\default_artwork.png") (Join-Path $dataDir "assets\default_artwork.png")
Copy-Item -Recurse (Join-Path $root "ui\dist") (Join-Path $dataDir "ui")
Copy-Item (Join-Path $root "config.example.toml") (Join-Path $dataDir "config.toml")
Copy-Item (Join-Path $PSScriptRoot "dist-readme.txt") (Join-Path $dist "README.txt")

Write-Host "`nBuilt + staged in $dist" -ForegroundColor Green
Get-ChildItem -Recurse -File $dist | ForEach-Object {
    "  $($_.FullName.Substring($dist.Length + 1))"
}
