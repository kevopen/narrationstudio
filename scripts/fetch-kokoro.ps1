#Requires -Version 5.1
# Fetch Kokoro TTS runtime files (idempotent — skips what exists):
#   third_party/onnxruntime/  headers + .lib (build) — ~300MB unpacked
#   models/kokoro/            int8 model + voices + vocab — ~140MB
#   third_party/espeak-ng/    G2P library + data — ~15MB
# Re-run cmake --preset release afterwards so Kokoro compiles in.
param(
  [string]$OrtVersion = "1.29.0",
  [string]$Repo = (Resolve-Path (Join-Path $PSScriptRoot ".."))
)
$ErrorActionPreference = "Stop"

$ortDir = Join-Path $Repo "third_party\onnxruntime"
$kokoroDir = Join-Path $Repo "models\kokoro"
$espeakDir = Join-Path $Repo "third_party\espeak-ng"
New-Item -ItemType Directory -Force $ortDir, $kokoroDir, $espeakDir | Out-Null

function Get-IfMissing($url, $dest) {
  if (Test-Path $dest) { Write-Host "exists: $dest" -ForegroundColor DarkGray; return }
  Write-Host "downloading $(Split-Path $dest -Leaf)…" -ForegroundColor Cyan
  Invoke-WebRequest -Uri $url -OutFile $dest
}

# 1. ONNX Runtime (headers + import lib + dll)
if (!(Test-Path (Join-Path $ortDir "include\onnxruntime_cxx_api.h"))) {
  $zip = Join-Path $env:TEMP "ort-win.zip"
  Get-IfMissing "https://github.com/microsoft/onnxruntime/releases/download/v$OrtVersion/onnxruntime-win-x64-$OrtVersion.zip" $zip
  Write-Host "extracting onnxruntime…" -ForegroundColor Cyan
  $tmp = Join-Path $env:TEMP "ort-out"
  Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
  Expand-Archive -LiteralPath $zip -DestinationPath $tmp -Force
  $inner = Get-ChildItem $tmp -Directory | Select-Object -First 1
  Move-Item (Join-Path $inner.FullName "include") (Join-Path $ortDir "include") -Force
  Move-Item (Join-Path $inner.FullName "lib") (Join-Path $ortDir "lib") -Force
  Remove-Item -Recurse -Force $tmp
  # PDBs are huge and unneeded for voice synthesis
  Get-ChildItem $ortDir -Recurse -Filter "*.pdb" | Remove-Item -Force
}

# 2. Kokoro model + voices + vocab (full-precision for Python-parity quality)
Get-IfMissing "https://github.com/thewh1teagle/kokoro-onnx/releases/download/model-files-v1.1/kokoro-v1.0.onnx" (Join-Path $kokoroDir "kokoro-v1.0.onnx")
Get-IfMissing "https://github.com/thewh1teagle/kokoro-onnx/releases/download/model-files-v1.1/voices-v1.0.bin" (Join-Path $kokoroDir "voices-v1.0.bin")
Copy-Item (Join-Path $Repo "resources\kokoro_vocab.json") (Join-Path $kokoroDir "kokoro_vocab.json") -Force -ErrorAction SilentlyContinue

# 3. espeak-ng (G2P): prefer pip's loader wheel (exact dll+data kokoro-onnx uses),
#    fall back to extracting the upstream MSI.
$needEspeak = !(Test-Path (Join-Path $espeakDir "espeak-ng.dll")) -and !(Test-Path (Join-Path $espeakDir "libespeak-ng.dll"))
if ($needEspeak) {
  $got = $false
  try {
    pip install -q espeakng-loader 2>$null
    $py = python -c "import espeakng_loader; print(espeakng_loader.get_library_path()); print(espeakng_loader.get_data_path())"
    if ($py.Count -ge 2 -and (Test-Path $py[0])) {
      Copy-Item $py[0] $espeakDir -Force
      Copy-Item -Recurse $py[1] (Join-Path $espeakDir "espeak-ng-data") -Force
      $got = $true
    }
  } catch { $got = $false }
  if (!$got) {
    Write-Host "pip path failed, extracting upstream MSI…" -ForegroundColor Yellow
    $msi = Join-Path $env:TEMP "espeak.msi"
    Get-IfMissing "https://github.com/espeak-ng/espeak-ng/releases/latest/download/espeak-ng.msi" $msi
    $tgt = Join-Path $env:TEMP "espeak-msi"
    Remove-Item -Recurse -Force $tgt -ErrorAction SilentlyContinue
    Start-Process msiexec -ArgumentList "/a `"$msi`" /qb TARGETDIR=`"$tgt`"" -Wait
    $dll = Get-ChildItem $tgt -Recurse -Filter "libespeak-ng.dll" | Select-Object -First 1
    $data = Get-ChildItem $tgt -Recurse -Directory -Filter "espeak-ng-data" | Select-Object -First 1
    if ($dll) { Copy-Item $dll.FullName (Join-Path $espeakDir "libespeak-ng.dll") -Force }
    if ($data) { Copy-Item -Recurse $data.FullName (Join-Path $espeakDir "espeak-ng-data") -Force }
  }
}

Write-Host "done. Reconfigure: cmake --preset release" -ForegroundColor Green
Get-ChildItem $kokoroDir | Select-Object Name, @{N="MB";E={[math]::Round($_.Length/1MB,1)}} | Format-Table -AutoSize | Out-String -Width 120 | Write-Host
