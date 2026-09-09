# Manga Narrator Studio — C++ / Qt Architecture & Development Guide

> How the current Python studio works (every flow we built), and how to rebuild it as a fast, modern C++ Qt editor — performance-first, UI-first, not an afterthought.

---

## 1. What We Built (Python baseline)

All paths under `C:/Users/KEVIN/Desktop/mangastudio`.

| Area | File(s) | What it does |
|---|---|---|
| Import | `app/core/pipeline.py:14`, `app/services/image_service.py:14`, `app/services/webtoon_service.py:110`, `app/services/mangadx_service.py:280` | PDF via `fitz` → `temp/pages`, image folder `.png/.jpg/.jpeg/.webp`, Webtoon via `webtoon-downloader` CLI, MangaDx via `urllib` + `/at-home/server`. |
| OCR | `app/services/ocr_service.py:131`, `app/ui/workers.py:511` | `PaddleOcrEngine` mobile (`PP-OCRv5_mobile`) batch `predict([paths])`, `MangaOcrEngine` ja, `sort_blocks_reading_order`. Worker reports absolute page numbers. |
| Caption | `app/services/caption_service.py:47`, `app/ui/workers.py:560` | `Florence-2-base` `<MORE_DETAILED_CAPTION>` via `torch`/`transformers`, `ThreadPoolExecutor(2)` — GIL released in `model.generate()`. |
| Cast / Synopsis | `app/ui/main_window.py:424` `_synopsis`, `app/database/database.py:34` `synopsis`, `page_characters` table, `app/services/ai_service.py:57` `PageContext.visible_characters` | Optional synopsis injected at prompt top, per-page `Visible: MC, Yuki` pills under viewer (`main_window.py:526` `cast_chips`), persisted via `page_cast`. |
| Narrate | `app/services/ai_service.py:100` `build_narration_prompt`, `743` `GeminiNarrator`, `862` `OllamaNarrator`, `app/ui/workers.py:212` `AiNarratorWorker`/`ChapterNarratorWorker` | Prompt = single narrator, OCR-error tolerance, paraphrase + pivotal-quote rules, dry humor guard, `[PAGE n]` tags mandatory. Chunked by `largest_fitting_chunk_end` to `narration_max_prompt_tokens` with token+request pacers, failover. |
| TTS | `app/services/tts_service.py:222` `Piper`/`Kokoro`, `app/ui/workers.py:669` `TtsWorker` | `KokoroTtsService` 24kHz + `split_pattern`, `Piper` raw PCM via `BytesIO`, `_promote_wav` copy-fallback on `WinError 5` when `QMediaPlayer` holds `narration.wav`. Measured `boundaries` → `markers`. |
| Timeline | `app/core/timeline.py:31` `markers_to_durations`, `app/ui/timeline_widget.py:414` | Waveform, draggable markers, `PLAY`/`Prev/Next`/`Insert marker`/`Page durations…`, karaoke `sentence_timings`. |
| Export | `app/services/video_service.py:97` `VideoService.export_slideshow` | Pre-compose `temp/frames/frame_*.jpg` via `_cover`/`_contain`/`blur`, `ffmpeg` concat demuxer `imageio_ffmpeg`, `scale=trunc(iw/2)*2`, `-shortest`. |
| DB | `app/database/database.py:13` `SCHEMA`, `app/database/projects.py:69` | `projects/pages/text_blocks/characters/timeline/narration_segments/page_characters`, WAL, `_migrate` adds `work_start/work_end/synopsis`. |
| UI | `app/ui/main_window.py:413` `MainWindow`, `app/ui/dialogs.py`, `app/ui/theme.py` | 1280×840, left `page_list`, center viewer + zoom + `page_desc` + `Cast:` pills + `Preview page`, right tabs `blocks/narration/review`, bottom `TimelineWidget`. Dark `DARK_QSS` app-level, `QMenuBar` File/Edit/Tools/View, toolbar 15 actions in 4 groups + separators. Icons via `QStyle.StandardPixmap`. |
| Autosave | `main_window.py:_autosave` | After Import/OCR/Describe/Narrate/TTS → `projects/autosave.db` reuse `_autosave_id`. |
| Episode scan | `main_window.py:check_mangadx_updates`/`check_webtoon_updates` | MangaDx polls `get_chapters` paging, diffs `known_ids`, Webtoon re-runs `--latest` diff, incremental append keeps OCR blocks and sets `Work Range` to new pages only. |

### End-to-end flow (as shipped)

```
PDF / Folder / MangaDx / Webtoon
  → pipeline.import_source → temp/pages or temp/{mangadx,webtoon}/<slug>
  → pages=[{path, description, blocks[], cast[]}]
  → OCR (active Work Range only) → blocks
  → Describe (Florence-2) → description (textless-critical)
  → Characters… / Synopsis… / Cast pills (per-page Visible)
  → Narration: build NarrationContext(title, characters, pages[range], synopsis, visible) → build_narration_prompt → chunked LLM → parse_tagged_narration → [PAGE n] segments → narration_edit
  → Synthesize: _page_texts_for → TtsWorker.synthesize_pages → measured boundaries → Timeline.set_audio
  → Export: markers_to_durations → VideoService → MP4 (black/blur/image + contain)
```

---

## 2. C++ / Qt Re-architecture — Principles

**Goal:** Same flows, editor-grade responsiveness, 1k pages never blocks UI. Python orchestration is ~1% of time; kernels are already C++ (libtorch, Paddle Inference, Piper, FFmpeg) — C++ wins on orchestration, zero-copy, thread pinning, and packaging, not on matmuls.

**Stack**

* **Qt 6.8+ Widgets** — keep Widgets (not QML) for pixel-perfect editor, `QMainWindow` + `QDockWidget` + `QSplitter`.
* **Build:** `CMake 3.27` + `vcpkg` + `Qt CMake`, `ninja`.
* **Inference:** `libtorch 2.4` / `ONNX Runtime 1.18` (quantized Florence-2 INT8), `Paddle Inference 3.0` (or `ONNX` RapidOCR), `piper` C API, `espeak-ng` + `libsoundfile`.
* **Media:** `FFmpeg 6.x` via `find_package(FFmpeg)` (bundled `imageio-ffmpeg` equivalent), `libsndfile`.
* **DB:** `SQLite` via `QtSql` / `sqlpp11`, WAL.
* **Net:** `QtNetwork` `QNetworkAccessManager` (replaces `urllib`, adds HTTP/2, retries, handshake timeout handling already added in `mangadx_service.py`).

### Module map (1:1 with Python)

```
src/
  core/
    pipeline.*      // import_source, pdf_to_images (MuPDF C API / QtPDF)
    timeline.*      // markers_to_durations, compute_waveform (libsndfile)
    settings.*      // QSettings + studio_config.json, page_zoom, voice, synopsis
    project.*       // Project model
  services/
    ocr_service.*       // Paddle Inference session, runBatch(vector<Path>)
    caption_service.*   // ONNX Florence-2 session, describe(path)->string
    character_service.* // (phase 2) face crop + CLIP embedding + k-means gallery
    ai_service.*        // buildNarrationPrompt, NarrationContext, Gemini/Ollama QNetwork streams, failover
    mangadx_service.*   // QNetworkAccessManager, searchManga/browseManga/getChapters/downloadChapters, cover cache temp/mangadx/.covers
    webtoon_service.*   // QProcess webtoon-downloader or native QNetwork
    tts_service.*       // Piper/Kokoro, KokoroTtsService (KPipeline), _promoteWav with QFile::rename fallback to copy
    video_service.*     // _cover/_contain/_renderFrame (QImage), exportSlideshow QProcess ffmpeg
  database/
    database.*      // SCHEMA, _migrate, page_characters
    projects.*      // saveProject/loadProject with page_cast
  ui/
    theme.*         // DARK_QSS as QSS file, applyDarkTheme(QApplication*)
    main_window.*   // QMainWindow, menus, toolbars, autosave, episode scan, cast chips, preview player
    dialogs.*       // CharactersDialog, SynopsisDialog, MangaDxDialog (thumbs + preview + Popular/Latest), WebtoonDialog, ExportDialog, PageDurationsDialog
    timeline_widget.* // QMediaPlayer/QAudioOutput, waveform QPainter, markers
    workers.*       // QThreadPool + QRunnable/QFuture, QThread workers (replaced by QtConcurrent)
```

### Threading — the perf win

* **QtConcurrent + QThreadPool** global pool `idealThreadCount()` (not `QThread IdlePriority` per worker). `CaptionWorker` → `QtConcurrent::mapped` with 2-4 runnables sharing one `OrtSession` (same as Python's `ThreadPoolExecutor(2)` but with `QFutureWatcher` for progress).
* **Zero-copy image decode:** `QImageReader` downscale at `max_side=2048` in worker thread, `QPixmap::fromImage` only on GUI thread — same as `_load_page_pixmap:922`.
* **Paced LLM:** `QTimer` + token/request window pacers identical to `workers.py:80` `_TokenWindowPacer`/`_RequestWindowPacer`, `retry in Xs` capped at 90s.

### UI — built as editor, not retrofitted

* **Menu bar first:** `File` (MangaDx/Webtoon/PDF/Folder → Save/Load → Export → Check for new …), `Edit` (Work Range/Characters/Synopsis/Voice/Zoom), `Tools` (OCR/Describe/Narrate/Synthesize/Cancel), `View` (toolbar toggle, dark/light). Toolbar becomes 6 icon-only quick actions (`QToolButton` + `QStyle::SP_MediaPlay` etc.) — file `main_window.cpp` `_buildActions` with `addSeparator`, icons replace emoji (`✨` → `SP_MediaPlay`).
* **Panels:** `QDockWidget` for Page List (left), Viewer+Cast (center), Blocks/Narration/Review+Cast Gallery (right), Timeline (bottom). Splitters persist via `QSettings`. `cast_chips` → `QHBoxLayout` of `QPushButton` checkable pills (`checked { background:#3a5a40 }`), `Preview page` → scratch `QMediaPlayer` (`_preview_player`) playing `temp/preview/page_{n}.wav` outside timeline — same as implemented.
* **Dark QSS** `src/ui/theme.qss` — `QDialog/QMessageBox #1e2332`, `QPushButton #2b3240/#e8ecf2`, `QHeaderView #1e2332/#9aa8c0`, `QTabBar` — applied once via `QApplication::setStyleSheet`. No per-widget `setStyleSheet` except `QWidget#timelineWidget` override (kept). Fix for white-on-white was removing the light `QPushButton { #f1f3f6 }` window sheet (`main_window.cpp:446`) that leaked `color:#e8ecf2`.

### Data — same schema, faster

* Add `page_characters (page_id, character_id PK)` and `synopsis TEXT` via `_migrate` — already done. In C++ use `QSqlQuery` `ALTER TABLE ADD COLUMN` if `PRAGMA table_info` lacks it.
* `saveProject` is one `QSqlDatabase::transaction()` — insert `characters` → `pages` → `text_blocks` + `page_characters` (`INSERT OR IGNORE`). Load joins `characters` then `page_characters` → `page_cast`.

### Where C++ actually speeds up (and where it doesn't)

* **Wins:** Image pipeline (`QImage` + `libjpeg-turbo` vs PIL), SQLite transactions (no Python GIL), `QNetworkAccessManager` HTTP/2 + keep-alive for MangaDx bulk `baseUrl/data/hash` pulls (vs `urllib` per-url open + retry), `FFmpeg` `QProcess` piping without `tempfile.TemporaryDirectory` Python overhead, `ONNX Runtime` INT8 Florence-2 30-40% faster than `transformers` Python, thread pool tuning (`maxWorkers = idealThreadCount()-1`).
* **No win:** LLM tokens (network), Paddle matmuls (already C++), Kokoro vocoding (libtorch). Rewriting those in C++ saves ~5-10% glue, not hours — the `thorough` vs `quick` worker split already captures it.

### Build & distribution

* `CMake` presets `dev`/`release`, `windeployqt` + `vcpkg` `libtorch` + `onnxruntime` + `paddle` + `ffmpeg` bundled. First run downloads only Piper voices (`models/`) on demand; vendor `models/` zip in installer to cut 1.35 GB first-run (Florence 750MB + Kokoro 330MB + Paddle 180MB) to 0 min on 50 Mbps offline installer.
* Auto-update via `WinSparkle` / `Paddle` license check at launch (`PADDLE_LICENSE_KEY`).

### Keep it built-in

* Every new table/column ships with `_migrate` (no manual `CREATE`).
* Every worker is `QRunnable` with `progress/status/error/done` signals and `requestInterruption()` cancel (same contract as `PipelineWorker:167`).
* Every heavy step ends with `_autosave()` to `projects/autosave.db` reusing `_autosaveId` (prevents unbounded rows), plus `File → Check for new …` incremental append that preserves `blocks/descriptions/cast` and sets `Work Range` to new pages only — so 3k-page manhwa is `1-1000` → export part1 → `Check` → new 50 pages become `1001-1050` range, no redo from p1.

---

*This is the spec to implement: same flows, C++ pools + ONNX INT8 + QSS editor, no feature as afterthought.*
