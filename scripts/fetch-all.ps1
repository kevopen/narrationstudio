#Requires -Version 5.1
# Fetch every runtime dependency (idempotent): ONNX Runtime + Kokoro voice +
# OCR + Florence captioning + ffmpeg. Used by the release workflow and by
# anyone building the offline installer.
param([string]$Repo = (Resolve-Path (Join-Path $PSScriptRoot "..")))
$ErrorActionPreference = "Stop"
& (Join-Path $PSScriptRoot "fetch-kokoro.ps1")
& (Join-Path $PSScriptRoot "fetch-ocr.ps1")
& (Join-Path $PSScriptRoot "fetch-florence.ps1")
& (Join-Path $PSScriptRoot "fetch-ffmpeg.ps1")
Write-Host "all runtime files ready." -ForegroundColor Green
