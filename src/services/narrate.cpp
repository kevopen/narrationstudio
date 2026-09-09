#include "narrate.h"
#include <QElapsedTimer>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QThread>
#include <stdexcept>

namespace NS {
namespace {

void sleepCancellable(int ms, const std::function<bool()> &cancelled) {
  int left = ms;
  while (left > 0) {
    if (cancelled && cancelled())
      throw std::runtime_error("cancelled");
    const int step = qMin(200, left);
    QThread::msleep(static_cast<unsigned long>(step));
    left -= step;
  }
}

struct TokenPacer {
  int limit; bool active = false;
  QVector<QPair<qint64,int>> reqs; // (steady ms, tokens)
  explicit TokenPacer(int perMinute) : limit(qMax(1, perMinute)) {}
  void note() { active = true; }
  static qint64 now() {
    static QElapsedTimer t;
    if (!t.isValid()) t.start();
    return t.elapsed();
  }
  void prune() {
    const qint64 n = now();
    for (qsizetype i = reqs.size() - 1; i >= 0; --i)
      if (n - reqs[i].first >= 60000) reqs.removeAt(i);
  }
  void wait(int tokens, const std::function<bool()> &cancelled) {
    if (!active) return;
    prune();
    int used = 0;
    for (auto &r : reqs) used += r.second;
    int need = tokens + used - limit;
    for (auto &r : reqs) {
      if (need <= 0) break;
      if (r.second >= need) {
        const int delay = static_cast<int>(60000 - (now() - r.first));
        if (delay > 0) sleepCancellable(delay, cancelled);
        break;
      }
      need -= r.second;
    }
    prune();
  }
  void record(int tokens) { reqs.append({now(), tokens}); }
};

struct RequestPacer {
  int limit; bool active = false;
  QVector<qint64> times;
  explicit RequestPacer(int perMinute) : limit(qMax(1, perMinute)) {}
  void note() { active = true; }
  void wait(const std::function<bool()> &cancelled) {
    if (!active) return;
    const qint64 n = TokenPacer::now();
    for (qsizetype i = times.size() - 1; i >= 0; --i)
      if (n - times[i] >= 60000) times.removeAt(i);
    if (times.size() >= static_cast<qsizetype>(limit)) {
      // oldest request leaves the window first (times unsorted-safe: use min)
      qint64 oldest = times.first();
      for (qint64 t : times) oldest = qMin(oldest, t);
      const int delay = static_cast<int>(60000 - (n - oldest));
      if (delay > 0) sleepCancellable(delay, cancelled);
    }
  }
  void record() { times.append(TokenPacer::now()); }
};

} // namespace

int estimatePromptTokens(const QString &text) {
  return qMax(1, text.size() / 4);
}

bool isTooLargeError(const QString &message) {
  static const QRegularExpression re(
    "request too large|too large|context length|maximum context|"
    "tokens per minute|token limit|input too long",
    QRegularExpression::CaseInsensitiveOption);
  return re.match(message).hasMatch();
}

bool isRateLimitError(const QString &message) {
  static const QRegularExpression re(
    "rate limit|429|quota|resource[_ ]exhausted|too many requests|exceeded",
    QRegularExpression::CaseInsensitiveOption);
  return re.match(message).hasMatch();
}

bool isQuotaExhaustedError(const QString &message);

QString failoverReason(const QString &message) {
  if (message.isEmpty()) return {};
  static const QRegularExpression busy(
    "overload|high demand|at\\s+capacity|saturated|is?\\s+busy|"
    "temporar\\w*\\s+unavailab\\w*|deployment[^\\n]*unavailab\\w*|"
    "service\\s+(?:currently\\s+)?unavailable|"
    "(?:server|backend)\\s+(?:error|unavailable)|\\b503\\b",
    QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression gone(
    "model\\s+not\\s+found|unknown\\s+model|unsupported\\s+model|"
    "does\\s+not\\s+exist|no\\s+such\\s+model|is\\s+not\\s+found|"
    "model[^\\n]*not\\s+available|not\\s+(?:yet\\s+)?supported|\\b404\\b",
    QRegularExpression::CaseInsensitiveOption);
  if (busy.match(message).hasMatch()) return "overloaded";
  if (gone.match(message).hasMatch()) return "unavailable";
  return {};
}

double retryAfterSeconds(const QString &message) {
  static const QRegularExpression re(
    "retry in\\s+([\\d.]+)\\s*s", QRegularExpression::CaseInsensitiveOption);
  auto m = re.match(message);
  if (!m.hasMatch()) return -1;
  bool ok = false;
  const double v = m.captured(1).toDouble(&ok);
  return ok ? qMax(0.0, v) : -1;
}

bool isQuotaExhaustedError(const QString &message) {
  if (message.isEmpty()) return false;
  // An explicit long wait means the quota, not a window, is spent.
  if (retryAfterSeconds(message) > 90.0) return true;
  if (retryAfterSeconds(message) >= 0) return false; // short wait: ride it out
  static const QRegularExpression daily(
    "daily|per.day|midnight|billing|free tier.+exhaust|exhaust.+daily",
    QRegularExpression::CaseInsensitiveOption);
  return daily.match(message).hasMatch();
}

NarrateResult narrateChapter(
  const Project &p, int fromPage, int toPage, const QString &priorStory,
  NarrateBackend backend, NarrateLimits limits,
  std::function<void(int, const QString &)> progress,
  std::function<bool()> cancelled) {

  NarrateResult out;
  if (backend.models.isEmpty()) {
    out.failed = true;
    out.error = "No narration models configured.";
    return out;
  }
  QStringList models = backend.models;
  QStringList keys = backend.apiKeys;
  if (keys.isEmpty()) keys << ""; // keyless backends (Ollama, stub)
  int keyIdx = 0;
  QSet<int> deadKeys;
  int budget = qMax(512, backend.maxPromptTokens);
  bool budgetHalved = false;
  TokenPacer tokPacer(limits.tokensPerMinute);
  RequestPacer reqPacer(limits.requestsPerMinute);

  QString script;
  QVector<PageSegment> segments;
  int lastDone = fromPage - 1;
  const int span = qMax(1, toPage - fromPage + 1);
  int chunkStart = fromPage;
  int rlAttempts = 0; // consecutive rate-limit hits on the current chunk

  auto fail = [&](const QString &msg) {
    out.script = script;
    out.segments = segments;
    out.lastDone = lastDone;
    out.failed = true;
    out.error = msg;
    return out;
  };

  auto liveKey = [&]() -> int {
    for (int k = 0; k < keys.size(); ++k) {
      const int i = (keyIdx + k) % keys.size();
      if (!deadKeys.contains(i)) return i;
    }
    return -1;
  };
  auto rotateKey = [&](const QString &why) -> bool {
    // Returns false when every key is spent.
    deadKeys.insert(keyIdx);
    const int n = liveKey();
    if (n < 0) return false;
    keyIdx = n;
    progress((chunkStart - fromPage) * 100 / span,
             QString("Key %1/%2 exhausted (%3) - switching to key %4, same chunk.")
             .arg(deadKeys.size()).arg(keys.size()).arg(why).arg(keyIdx + 1));
    return true;
  };

  try {
    while (chunkStart <= toPage) {
      if (cancelled && cancelled()) break;
      int chunkEnd = Narrator::largestFittingChunkEnd(
        p, chunkStart, budget * 4);
      chunkEnd = qMin(chunkEnd, toPage);
      chunkEnd = qMin(chunkEnd, chunkStart + qMax(1, limits.chunkPages) - 1);
      chunkEnd = qMax(chunkEnd, chunkStart);

      QString prompt = Narrator::buildPrompt(p, chunkStart, chunkEnd, script.isEmpty() ? priorStory : script);
      const int tokens = estimatePromptTokens(prompt);
      if (tokPacer.active || reqPacer.active)
        progress((chunkStart - fromPage) * 100 / span,
                 QString("Pacing pages %1-%2 to stay within limits...").arg(chunkStart).arg(chunkEnd));
      reqPacer.wait(cancelled);
      tokPacer.wait(tokens, cancelled);

      // --- one chunk, with failover across the model chain (and key rotation)
      QString raw, chunkErr;
      QStringList chunkModels = models;
      QStringList events;
      int mi = 0;
      while (mi < chunkModels.size()) {
        const QString model = chunkModels[mi];
        try {
          raw = backend.request(model, keys[qMax(0, keyIdx)], prompt);
          break;
        } catch (const std::exception &e) {
          chunkErr = QString::fromUtf8(e.what());
          const QString reason = failoverReason(chunkErr);
          if (reason == "unavailable") {
            events << QString("%1 dropped (%2)").arg(model, reason);
            chunkModels.removeAt(mi);
            if (chunkModels.isEmpty()) {
              return fail("All narration models were rejected as unavailable "
                          "(last: " + chunkErr.left(240) + "). Old IDs get retired - "
                          "current flash IDs: gemini-3.6-flash, gemini-3.7-flash, "
                          "gemini-3.8-flash, gemini-3.5-flash, gemini-flash-latest. "
                          "Update them in Edit > Settings.");
            }
            continue;
          }
          if (reason == "overloaded" && mi + 1 < chunkModels.size()) {
            events << QString("%1 overloaded - switched to %2").arg(model, chunkModels[mi + 1]);
            ++mi;
            continue;
          }
          if (reason == "overloaded") {
            return fail("Every narration model is overloaded right now - wait a "
                        "few minutes and try again. Last error: " + chunkErr.left(240));
          }
          break; // quota / auth / other: handled below, no model switch
        }
      }
      for (const QString &ev : events)
        progress((chunkStart - fromPage) * 100 / span,
                 QString("Pages %1-%2: %3.").arg(chunkStart).arg(chunkEnd).arg(ev));
      models = chunkModels; // permanently dropped models stay dropped

      if (!chunkErr.isEmpty() && raw.trimmed().isEmpty()) {
        const bool tooLarge = isTooLargeError(chunkErr);
        const bool rateLimit = isRateLimitError(chunkErr);
        if (!tooLarge && !rateLimit)
          return fail(chunkErr);
        if (tooLarge && !budgetHalved) {
          budgetHalved = true;
          budget = qMax(512, budget / 2);
          sleepCancellable(2000, cancelled);
          progress((chunkStart - fromPage) * 100 / span,
                   QString("Pages %1-%2: request exceeded the limit - shrinking and retrying.")
                   .arg(chunkStart).arg(chunkEnd));
          continue;
        }
        tokPacer.note();
        reqPacer.note();
        const double retryIn = retryAfterSeconds(chunkErr);
        if (isQuotaExhaustedError(chunkErr)) {
          // This key is spent (long/daily quota) — rotate and retry the chunk.
          rlAttempts = 0;
          if (rotateKey(chunkErr.left(120)))
            continue;
          return fail(QString("Quota exhausted on all %1 key(s) - pages %2-%3 remain. "
                              "Quotas reset around midnight Pacific; try again later, "
                              "or add keys in Edit > Settings.")
                      .arg(keys.size()).arg(chunkStart).arg(chunkEnd));
        }
        // attempts counted per chunk: consecutive rate-limit hits
        ++rlAttempts;
        if (rlAttempts > 10) {
          rlAttempts = 0;
          // A 429 storm usually means quota too — try the next key first.
          if (rotateKey("still limited after 10 waits"))
            continue;
          return fail("Provider kept rejecting requests for pages " +
                      QString("%1-%2: %3").arg(chunkStart).arg(chunkEnd)
                      .arg(chunkErr.left(220)));
        }
        const double delay = (retryIn >= 0 ? retryIn + 2 : 30.0);
        progress((chunkStart - fromPage) * 100 / span,
                 QString("Pages %1-%2: rate limit - waiting %3s.")
                 .arg(chunkStart).arg(chunkEnd).arg(qint64(delay)));
        sleepCancellable(static_cast<int>(delay * 1000), cancelled);
        continue;
      }

      tokPacer.record(tokens);
      reqPacer.record();
      rlAttempts = 0; // success breaks the rate-limit streak

      auto [clean, segs] = Narrator::parseTagged(raw, chunkStart, chunkEnd);
      if (clean.trimmed().isEmpty())
        return fail(QString("No narration returned for pages %1-%2.").arg(chunkStart).arg(chunkEnd));
      // coverage check: regenerate once when most markers dropped
      QSet<int> tagged;
      for (auto &s : segs) tagged.insert(s.page);
      QVector<int> missing;
      for (int n = chunkStart; n <= chunkEnd; ++n)
        if (!tagged.contains(n)) missing << n;
      if (!missing.isEmpty()
          && tagged.size() * 2 < static_cast<qsizetype>(chunkEnd - chunkStart + 1)) {
        progress((chunkStart - fromPage) * 100 / span,
                 QString("Pages %1-%2: markers dropped for %3 pages - regenerating once.")
                 .arg(chunkStart).arg(chunkEnd).arg(missing.size()));
        QString raw2, err2;
        try {
          raw2 = backend.request(models.first(), keys[qMax(0, keyIdx)], prompt);
        } catch (const std::exception &e) { err2 = QString::fromUtf8(e.what()); }
        if (!err2.isEmpty() || raw2.trimmed().isEmpty()) {
          // keep first try
        } else {
          auto [clean2, segs2] = Narrator::parseTagged(raw2, chunkStart, chunkEnd);
          QSet<int> tagged2;
          for (auto &s : segs2) tagged2.insert(s.page);
          int missing2 = 0;
          for (int n = chunkStart; n <= chunkEnd; ++n)
            if (!tagged2.contains(n)) ++missing2;
          if (missing2 <= missing.size()) {
            clean = clean2; segs = segs2;
          }
        }
      }
      if (!script.isEmpty()) script += "\n\n";      const int offset = script.size();
      script += clean;
      for (auto &s : segs)
        segments.append({s.page, s.start + offset, s.end + offset});
      lastDone = chunkEnd;
      chunkStart = chunkEnd + 1;
      progress((chunkStart - fromPage) * 100 / span,
               QString("Narrated pages %1-%2.").arg(fromPage).arg(qMin(chunkStart - 1, toPage)));
    }
  } catch (const std::exception &e) {
    return fail(QString::fromUtf8(e.what()));
  }
  out.script = script;
  out.segments = segments;
  out.lastDone = lastDone;
  return out;
}

bool narrationSegmentsValid(const QString &script,
                              const QVector<PageSegment> &segments,
                              int fromPage, int toPage) {
  if (segments.isEmpty()) return false;
  const int n = qMax(1, toPage - fromPage + 1);
  int maxEnd = 0;
  QSet<int> pages;
  for (auto &s : segments) {
    if (s.page < fromPage || s.page > toPage
        || s.start < 0 || s.end < s.start || s.end > script.size())
      return false;
    pages.insert(s.page);
    maxEnd = qMax(maxEnd, s.end);
  }
  return maxEnd == script.size() && pages.size() <= n;
}

QVector<QString> pageSlices(const QString &script,                           const QVector<PageSegment> &segments,
                           int fromPage, int toPage) {
  const int n = qMax(1, toPage - fromPage + 1);
  QVector<QString> slices;
  slices.reserve(n);
  if (!narrationSegmentsValid(script, segments, fromPage, toPage)) {
    // Even fallback: split by characters (callers that voice audio should
    // prefer whole-script synthesis + proportional markers instead, so words
    // are never chopped — see runSynthesize).
    for (int i = 0; i < n; ++i) {
      const int a = static_cast<int>(qsizetype(i) * script.size() / n);
      const int b = static_cast<int>(qsizetype(i + 1) * script.size() / n);
      slices << script.mid(a, b - a);
    }
    return slices;
  }
  QMap<int, QPair<int,int>> byPage;
  for (auto &s : segments) byPage.insert(s.page, {s.start, s.end});
  int cursor = 0;
  for (int pg = fromPage; pg <= toPage; ++pg) {
    if (byPage.contains(pg)) {
      auto [a, b] = byPage[pg];
      // fill any gap with the previous slice (keeps audio continuous)
      if (a > cursor && !slices.isEmpty())
        slices.last() += script.mid(cursor, a - cursor);
      slices << script.mid(a, b - a);
      cursor = b;
    } else {
      slices << QString();
    }
  }
  // trailing prose after the last tag belongs to the final page
  if (cursor < script.size() && !slices.isEmpty())
    slices.last() += script.mid(cursor);
  return slices;
}

QPair<QString, QVector<PageSegment>> appendScript(
  const QString &oldScript, const QVector<PageSegment> &oldSegs,
  const QString &chunk, const QVector<PageSegment> &chunkSegs) {
  if (oldScript.trimmed().isEmpty()) return {chunk, chunkSegs};
  if (chunk.trimmed().isEmpty()) return {oldScript, oldSegs};
  const QString full = oldScript + "\n\n" + chunk;
  const int offset = oldScript.size() + 2;
  QVector<PageSegment> segs = oldSegs;
  for (auto &s : chunkSegs)
    segs.append({s.page, s.start + offset, s.end + offset});
  return {full, segs};
}

} // namespace NS
