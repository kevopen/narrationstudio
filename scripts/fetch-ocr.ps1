#Requires -Version 5.1
# Fetch English OCR models (idempotent): PP-OCRv3 detection + recognition
# (~13MB) from the rapidocr_onnxruntime wheel into models/ocr/.
param(
  [string]$PkgVersion = "1.2.3",
  [string]$Repo = (Resolve-Path (Join-Path $PSScriptRoot ".."))
)
$ErrorActionPreference = "Stop"

$ocrDir = Join-Path $Repo "models\ocr"
New-Item -ItemType Directory -Force $ocrDir | Out-Null

$needDet = !(Test-Path (Join-Path $ocrDir "ch_PP-OCRv3_det_infer.onnx"))
$needRec = !(Test-Path (Join-Path $ocrDir "ch_PP-OCRv3_rec_infer.onnx"))
if (!$needDet -and !$needRec) { Write-Host "OCR models present." -ForegroundColor DarkGray; exit 0 }

$tmp = Join-Path ([System.IO.Path]::GetTempPath()) "ns-rapidocr"
New-Item -ItemType Directory -Force $tmp | Out-Null
Write-Host "downloading rapidocr_onnxruntime $PkgVersion…" -ForegroundColor Cyan
pip download "rapidocr_onnxruntime==$PkgVersion" --no-deps -d $tmp
$whl = Get-ChildItem $tmp -Filter "*.whl" | Select-Object -First 1
if (!$whl) { throw "pip download failed - is Python/pip installed?" }
$zip = Join-Path $tmp "pkg.zip"
Copy-Item $whl.FullName $zip -Force
$pkg = Join-Path $tmp "pkg"
Remove-Item -Recurse -Force $pkg -ErrorAction SilentlyContinue
Expand-Archive -LiteralPath $zip -DestinationPath $pkg -Force
Copy-Item (Join-Path $pkg "rapidocr_onnxruntime\models\ch_PP-OCRv3_det_infer.onnx") $ocrDir -Force
Copy-Item (Join-Path $pkg "rapidocr_onnxruntime\models\ch_PP-OCRv3_rec_infer.onnx") $ocrDir -Force
Write-Host "done. Reconfigure: cmake --preset release" -ForegroundColor Green
Get-ChildItem $ocrDir | Select-Object Name, @{N="MB";E={[math]::Round($_.Length/1MB,1)}} | Format-Table -AutoSize | Out-String -Width 120 | Write-Host
