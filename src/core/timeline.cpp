#include "timeline.h"
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

namespace NS {

QVector<double> Timeline::markersToDurations(const QVector<double> &markers,
                                            double totalSeconds, int pageCount) {
  if (pageCount <= 0) return {};
  if (totalSeconds <= 0) return QVector<double>(pageCount, 0.0);

  // Sorted, in-range, dedupe near-identical (>=0.05s apart).
  QVector<double> sorted = markers;
  std::sort(sorted.begin(), sorted.end());
  QVector<double> bounds;
  for (double m : sorted) {
    if (!(m > 0.0 && m < totalSeconds)) continue;
    if (bounds.isEmpty() || m - bounds.last() >= 0.05) bounds.append(m);
  }
  if (bounds.size() > pageCount - 1) bounds.resize(pageCount - 1);

  QVector<double> points; points.append(0.0); points += bounds;
  QVector<double> durations; durations.reserve(pageCount);
  for (int i = 0; i < pageCount; ++i) {
    if (i < points.size() - 1) {
      durations.append(std::round((points[i+1] - points[i]) * 1000.0) / 1000.0);
    } else if (i == pageCount - 1) {
      durations.append(std::round((totalSeconds - points.last()) * 1000.0) / 1000.0);
    } else {
      double remaining = totalSeconds - points.last();
      int tailCount = pageCount - (static_cast<int>(points.size()) - 1);
      durations.append(std::round((remaining / tailCount) * 1000.0) / 1000.0);
    }
  }
  double sum = 0; for (int i = 0; i + 1 < durations.size(); ++i) sum += durations[i];
  durations.last() = std::round((totalSeconds - sum) * 1000.0) / 1000.0;
  return durations;
}

QVector<double> Timeline::durationsToMarkers(const QVector<double> &durations,
                                            double totalSeconds) {
  QVector<double> markers; if (totalSeconds <= 0) return markers;
  double acc = 0;
  for (int i = 0; i + 1 < durations.size(); ++i) {
    acc += std::max(0.05, durations[i]);
    if (acc >= totalSeconds - 0.05) break;
    markers.append(std::round(acc * 1000.0) / 1000.0);
  }
  return markers;
}

QString Timeline::stripPageMarkers(const QString &text) {
  static const QRegularExpression re(
    QStringLiteral("^[ \\t]*\\[?PAGE\\s*\\d+\\]?\\s*:?[ \\t]*$"),
    QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);
  QString out = text; out.remove(re); return out;
}

QVector<QPair<int,int>> Timeline::splitSentences(const QString &text) {
  QVector<QPair<int,int>> ranges; if (text.isEmpty()) return ranges;
  // Split after . ! ? … followed by whitespace; keep "..." inside one sentence.
  static const QRegularExpression re(QStringLiteral("(?<=[.!?\u2026])(?<![.][.])\\s+"));
  int start = 0;
  auto it = re.globalMatch(text);
  while (it.hasNext()) {
    auto m = it.next();
    int end = m.capturedStart();
    if (end > start) ranges.append({start, end});
    start = m.capturedEnd();
  }
  if (start < text.size()) ranges.append({start, (int)text.size()});
  return ranges;
}

} // namespace NS
