#Requires -Version 5.1
# Fetch a bundled ffmpeg (idempotent): gyan.dev essentials build (~100MB),
# ffmpeg.exe only, into third_party/ffmpeg/.
param(
  [string]$Repo = (Resolve-Path (Join-Path $PSScriptRoot ".."))
)
$ErrorActionPreference = "Stop"

$dir = Join-Path $Repo "third_party\ffmpeg"
New-Item -ItemType Directory -Force $dir | Out-Null
$exe = Join-Path $dir "ffmpeg.exe"
if (Test-Path $exe) { Write-Host "ffmpeg present." -ForegroundColor DarkGray; exit 0 }

$zip = Join-Path ([System.IO.Path]::GetTempPath()) "ns-ffmpeg.zip"
Write-Host "downloading ffmpeg essentials..." -ForegroundColor Cyan
curl.exe -sL -C - --retry 20 --retry-all-errors -o $zip `
  "https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip"
$tmp = Join-Path ([System.IO.Path]::GetTempPath()) "ns-ffout"
Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
Expand-Archive -LiteralPath $zip -DestinationPath $tmp -Force
$inner = Get-ChildItem $tmp -Directory | Select-Object -First 1
Copy-Item (Join-Path $inner.FullName "bin\ffmpeg.exe") $exe -Force
Remove-Item -Recurse -Force $tmp
Write-Host "done: $exe" -ForegroundColor Green
& $exe -version 2>$null | Select-Object -First 1 | Write-Host
