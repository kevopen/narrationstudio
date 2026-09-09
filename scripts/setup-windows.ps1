#Requires -Version 5.1
# One-time dev setup for NarrationStudio on Windows.
# Installs: MSVC build tools, CMake, Ninja, Qt 6.8.
param([string]$QtVersion = "6.8.2")

$ErrorActionPreference = "Stop"

Write-Host "== NarrationStudio setup ==" -ForegroundColor Cyan

function Need($cmd) { $null -eq (Get-Command $cmd -ErrorAction SilentlyContinue) }

if (Need "winget") { throw "winget not found. Install App Installer from Microsoft Store first." }

winget install --silent --accept-source-agreements --accept-package-agreements Kitware.CMake Ninja-build.NinjaBuild
winget install --silent --accept-source-agreements --accept-package-agreements Microsoft.VisualStudio.2022.BuildTools --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.Windows11SDK.22621"

# Qt via aqtinstall (lightweight, no Qt account needed)
pip install -q aqtinstall
$aqtArgs = "install-qt windows desktop $QtVersion win64_msvc2022_64 -O C:\Qt --archives qtbase qtmultimedia qtsvg qtimageformats"
Write-Host "Installing Qt: $aqtArgs"
Invoke-Expression "aqt $aqtArgs"

[Environment]::SetEnvironmentVariable("CMAKE_PREFIX_PATH", "C:\Qt\$QtVersion\msvc2022_64", "User")
Write-Host "Done. Restart terminal, then: cmake --preset release" -ForegroundColor Green
