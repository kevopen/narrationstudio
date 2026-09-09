#Requires -Version 5.1
# Fetch Florence-2-base captioning files (idempotent): int8 ONNX parts +
# tokenizer (~370MB) from onnx-community into models/florence/.
param(
  [string]$Repo = (Resolve-Path (Join-Path $PSScriptRoot ".."))
)
$ErrorActionPreference = "Stop"

$dir = Join-Path $Repo "models\florence"
New-Item -ItemType Directory -Force $dir | Out-Null
$base = "https://huggingface.co/onnx-community/Florence-2-base/resolve/main"
$files = @(
  "onnx/vision_encoder_int8.onnx",
  "onnx/encoder_model_int8.onnx",
  "onnx/embed_tokens_int8.onnx",
  "onnx/decoder_model_int8.onnx",
  "onnx/decoder_with_past_model_int8.onnx",
  "tokenizer.json", "vocab.json", "generation_config.json",
  "special_tokens_map.json", "tokenizer_config.json"
)
foreach ($f in $files) {
  $dest = Join-Path $dir (Split-Path $f -Leaf)
  if (Test-Path $dest) { Write-Host "exists: $(Split-Path $dest -Leaf)" -ForegroundColor DarkGray; continue }
  Write-Host "downloading $(Split-Path $f -Leaf)..." -ForegroundColor Cyan
  curl.exe -sL -C - --retry 20 --retry-all-errors -o $dest "$base/$f"
}
Write-Host "done. Reconfigure: cmake --preset release" -ForegroundColor Green
Get-ChildItem $dir | Select-Object Name, @{N="MB";E={[math]::Round($_.Length/1MB,1)}} | Format-Table -AutoSize | Out-String -Width 150 | Write-Host
