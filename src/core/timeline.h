#pragma once
#include <QString>
#include <QStringList>
#include <QVector>

namespace NS {

// Pure logic (no QtWidgets) — mirrors mangastudio/app/core/timeline.py.
// Markers = seconds where a new page starts (page 1 starts at 0).
class Timeline {
public:
  static QVector<double> markersToDurations(const QVector<double> &markers,
                                            double totalSeconds, int pageCount);
  static QVector<double> durationsToMarkers(const QVector<double> &durations,
                                            double totalSeconds);
  static QString stripPageMarkers(const QString &text);
  // (startChar,endChar) per sentence; karaoke highlight.
  static QVector<QPair<int,int>> splitSentences(const QString &text);
};

} // namespace NS
