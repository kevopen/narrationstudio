#Requires -Version 5.1
# Build + windeployqt + portable zip/NSIS installer.
param([string]$Preset = "release")
$ErrorActionPreference = "Stop"
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
# (when run from repo root scripts/ folder, $PSScriptRoot is scripts/)
$repo = Resolve-Path (Join-Path $PSScriptRoot "..")
$build = Join-Path $repo "build\$Preset"
Write-Host "== Building preset $Preset ==" -ForegroundColor Cyan
cmake --preset $Preset
cmake --build --preset $Preset --config Release
$exe = Join-Path $build "NarrationStudio.exe"
if (!(Test-Path $exe)) { throw "Build output not found: $exe" }
Write-Host "== windeployqt ==" -ForegroundColor Cyan
$qtBin = Join-Path $env:CMAKE_PREFIX_PATH "bin"
& (Join-Path $qtBin "windeployqt.exe") --release --qmldir "$repo\src" "$exe"
Write-Host "== cpack ==" -ForegroundColor Cyan
Push-Location $build
cpack -G ZIP
cpack -G NSIS
Pop-Location
Write-Host "Done. Share build\$Preset\*.zip or the NSIS .exe installer." -ForegroundColor Green
