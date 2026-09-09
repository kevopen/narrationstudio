#pragma once
#include "core/project.h"
#include "services/ai_service.h" // Narrator::buildPrompt/parseTagged, PageSegment
#include <QString>
#include <QVector>
#include <functional>

// Chunked, paced, failover-capable narration — port of mangastudio's
// ChapterNarratorWorker tactics:
//  - per-backend token budget (Gemini huge context → ~100-page chunks;
//    token-capped providers → small chunks)
//  - TokenPacer + RequestPacer, inactive until the first rate-limit hit
//  - "too large" → halve budget once, retry smaller
//  - rate limit → honor the provider's "retry in Xs" (cap 90s), same chunk,
//    up to 10 retries; longer waits surface as quota-exhausted
//  - Gemini overloaded/unavailable → fail over to the next model mid-run
//  - markers dropped badly → regenerate the chunk once, keep the better try
//  - hard failure keeps the pages narrated so far (partial script, no wipe)
namespace NS {

int estimatePromptTokens(const QString &text); // ~1 token per 4 chars
bool isTooLargeError(const QString &message);
bool isRateLimitError(const QString &message);
// True when the error means "this key is spent" (retry horizon absurd or a
// daily quota), as opposed to "wait a bit on the same key". Triggers API-key
// rotation instead of waiting.
bool isQuotaExhaustedError(const QString &message);
// "overloaded" (try next model) | "unavailable" (drop model, try next) | ""
QString failoverReason(const QString &message);
// seconds from "retry in 22.07s", or -1
double retryAfterSeconds(const QString &message);

struct NarrateBackend {
  QString label;
  int maxPromptTokens = 4000; // Gemini 30000, Ollama 16000, generic OA 4000
  QStringList models;         // failover chain, primary first
  QStringList apiKeys;        // rotation pool, primary first ([""] = keyless)
  // Throws std::runtime_error(message) on failure.
  std::function<QString(const QString &model, const QString &key,
                        const QString &prompt)> request;
};

struct NarrateResult {
  QString script;
  QVector<PageSegment> segments;
  int lastDone = 0;
  bool failed = false;
  QString error;
};

struct NarrateLimits {
  int chunkPages = 100;
  int tokensPerMinute = 5500;
  int requestsPerMinute = 18;
};

NarrateResult narrateChapter(
  const Project &p, int fromPage, int toPage, const QString &priorStory,
  NarrateBackend backend, NarrateLimits limits,
  std::function<void(int, const QString &)> progress,
  std::function<bool()> cancelled);

// Continuation: merge a fresh chunk (offsets relative to the chunk) onto an
// existing script. Returns {fullScript, absoluteSegments}. Fresh ranges that
// start after all previously narrated pages append; re-narrated ranges are
// the caller's responsibility (it passes only the overlapping old segments).
QPair<QString, QVector<PageSegment>> appendScript(
  const QString &oldScript, const QVector<PageSegment> &oldSegs,
  const QString &chunk, const QVector<PageSegment> &chunkSegs);

// Slice the narration script into one text per page in [fromPage, toPage].
// Uses stored [PAGE n] segments when they still match the script; otherwise
// splits the script evenly (same fallback Python uses at synthesis time).
QVector<QString> pageSlices(const QString &script,
                           const QVector<PageSegment> &segments,
                           int fromPage, int toPage);

// True when segments exactly cover this script (edits invalidate them).
// Python compares byte-identical text; stale scripts fall back to whole-script
// synthesis with proportional markers instead of chopping words.
bool narrationSegmentsValid(const QString &script,
                            const QVector<PageSegment> &segments,
                            int fromPage, int toPage);

} // namespace NS
