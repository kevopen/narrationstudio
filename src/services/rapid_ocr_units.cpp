#include "rapid_ocr.h"
#include <algorithm>

// Pure-logic OCR units — no ONNX Runtime, always compiled (and unit-tested).

QPair<QString,double> RapidOcrEngine::ctcDecode(const float *logits, int seqLen,
                                               int numClasses,
                                               const QStringList &keys) {
  QString text;
  double confSum = 0;
  int kept = 0, prev = -1;
  for (int s = 0; s < seqLen; ++s) {
    const float *row = logits + static_cast<qsizetype>(s) * numClasses;
    int best = 0;
    float bestV = row[0];
    for (int c = 1; c < numClasses; ++c)
      if (row[c] > bestV) { bestV = row[c]; best = c; }
    if (best == 0) { prev = 0; continue; }       // blank
    if (best == prev) continue;                   // duplicate
    prev = best;
    // ids: 0=blank, 1..K=keys, K+1=' ' (port of CTCLabelDecode)
    if (best - 1 < keys.size()) text += keys[best - 1];
    else text += ' ';
    confSum += bestV; ++kept;
  }
  return {text, kept ? confSum / kept : 0.0};
}

QVector<QRect> RapidOcrEngine::sortReadingOrder(QVector<QRect> boxes) {
  // Rows top→bottom (vertical overlap ≥40% of smaller height), x left→right.
  struct Row { QVector<QRect> items; };
  std::sort(boxes.begin(), boxes.end(),
            [](const QRect &a, const QRect &b){ return a.top() < b.top(); });
  QVector<Row> rows;
  for (const QRect &b : boxes) {
    bool placed = false;
    for (Row &r : rows) {
      const QRect &f = r.items.first();
      const int overlap = std::min(b.bottom(), f.bottom())
                        - std::max(b.top(), f.top());
      const int need = static_cast<int>(
        0.4 * std::min(b.height(), f.height()));
      if (overlap >= need) { r.items.append(b); placed = true; break; }
    }
    if (!placed) rows.append({QVector<QRect>{b}});
  }
  QVector<QRect> out;
  for (Row &r : rows) {
    std::sort(r.items.begin(), r.items.end(),
              [](const QRect &a, const QRect &b){ return a.left() < b.left(); });
    out += r.items;
  }
  return out;
}
