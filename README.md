# NarrationStudio — C++ / Qt (fast + shareable)

Port of the Python `mangastudio` to a fast, beautiful, distributable C++ Qt editor.
See `ARCHITECTURE_CPP.md` for the full spec.

## Why this is better than the Python version

- **Speed:** `QtConcurrent` thread pool, `QImageReader` downscale off the GUI thread,
  single SQLite transaction + WAL, `QNetworkAccessManager` HTTP/2 keep-alive,
  `QImage` cover/contain/blur (libjpeg-turbo) instead of PIL.
- **Beautiful:** real editor layout (`QDockWidget` + `QSplitter` persisted in
  `QSettings`), one-file dark/light `theme.qss`, icon-only toolbar
  (`QStyle::StandardPixmap` — no emoji), checkable cast pills, karaoke timeline.
- **Shareable:** builds with **Qt only** by default. No 1.35 GB models, no torch
  needed to compile or run. `cpack -G ZIP` = portable folder,
  `cpack -G NSIS` = Windows installer. AI backends are opt-in
  (`-DENABLE_AI_BACKENDS=ON`).

## Quick start (Windows)

```powershell
# 1. One-time setup (installs Qt 6.8, CMake, Ninja, MSVC)
powershell -ExecutionPolicy Bypass -File scripts/setup-windows.ps1

# 2. Configure + build
cmake --preset release
cmake --build --preset release

# 3. Run
.\build\release\NarrationStudio.exe

# 4. Share: portable zip + installer
cd build/release
cpack -G ZIP
cpack -G NSIS
# or: powershell ..\..\scripts\package.ps1
```

`scripts/setup-windows.ps1` uses `winget` + `aqtinstall`. If you already have
Qt 6.8 + MSVC 2022, just set `CMAKE_PREFIX_PATH=C:\Qt\6.8.x\msvc2022_64`.

## Project layout

```
src/core/      pipeline.* timeline.* settings.* project.*
src/services/  ocr/caption/ai/tts/video/mangadx/webtoon — interface + stub,
               real logic where it needs no models (prompt builder, tag parser,
               chunking, QNetwork MangaDx, ffmpeg QProcess video)
src/database/  database.* projects.* (SCHEMA + _migrate + transactions)
src/ui/        main_window.* timeline_widget.* dialogs.* theme.* workers.*
resources/     app.qrc theme.qss
tests/         ctest: timeline math, narration tag parsing
```

## Voices — Kokoro TTS (real, local, 24kHz)

Step 5 synthesizes with **Kokoro v1.0** (ONNX int8, 54 voices) when its files
are present, otherwise a silence stub keeps the timeline working.

```powershell
# One-time download (~350MB: ONNX Runtime + full-precision model + voices + espeak-ng)
powershell -ExecutionPolicy Bypass -File scripts/fetch-kokoro.ps1
cmake --preset release
cmake --build --preset release
```

Then `Edit → Voice (Kokoro)…`: pick a voice (`af_heart` default narrator),
speed 50–200%, **Test voice** previews it, all persisted in settings.
Synthesis is sentence-chunked on the thread pool with *measured* per-sentence
boundaries → real timeline markers + karaoke timings (no estimates).

## OCR — English dialogue (real, local)

Step 2 runs **PP-OCRv3 DB detection + CRNN** (ONNX, English-only, no
classifier): connected-component boxes with dialogue-tuned padding, line
merging, batched recognition, CTC decode from the model's embedded keys,
reading-order sort. Blocks land in the Inspector with page rects.

```powershell
# One-time download (~13MB)
powershell -ExecutionPolicy Bypass -File scripts/fetch-ocr.ps1
cmake --preset release
cmake --build --preset release
```

Verified against the Python reference on identical art: `HELLO WORLD`,
`MANGA TEST` — parity.

## Describe — Florence-2 vision captions (real, local)

Step 3 runs **Florence-2-base `<MORE_DETAILED_CAPTION>`** as plain ONNX
(int8 parts, no extra runtime): 768px CLIP preprocess → DaViT vision →
prompt embed → encoder → autoregressive decoder with KV-cache past windows,
GPT-2 BPE written by hand, greedy + no-repeat-trigram. Per-page progress;
pages with notes (yours or earlier runs) are skipped, never overwritten.

```powershell
# One-time download (~370MB)
powershell -ExecutionPolicy Bypass -File scripts/fetch-florence.ps1
cmake --preset release
cmake --build --preset release
```

## Releasing (Windows)

```powershell
# tag a version -> GitHub Actions builds, tests, packages, publishes
git tag v0.2.0; git push origin v0.2.0
# (.github/workflows/release.yml: Qt 6.8 + MSVC, fetch-all models,
#  full ctest, CPack ZIP + NSIS -> Release assets)
```

Shipping model: **offline bundle** (~950MB: app + Qt + runtimes + all
models). Nothing downloads at runtime; every feature works offline.
`scripts/fetch-all.ps1` reproduces the runtime tree locally.

## Notes vs Python baseline

- Same SQLite schema (`projects/pages/panels/text_blocks/characters/timeline/
  narration_segments/page_characters`) + `ALTER TABLE` migrate for
  `work_start/work_end/synopsis`.
- Same narration prompt rules (single narrator, OCR-error tolerance,
  `[PAGE n]` tags) — see `src/services/ai_service.*`.
- Same timeline math (`markers_to_durations`, dedupe 0.05s, even tail split) —
  see `src/core/timeline.*`.
- Same video frame logic (`cover/contain/blur/black/white/image`,
  concat demuxer, `-shortest`) — see `src/services/video_service.*`.
- OCR/Caption/TTS-vocoder are **interfaces** (`IOcrEngine` etc.) with fast
  stubs now; plug ONNX/Torch later without touching UI.
