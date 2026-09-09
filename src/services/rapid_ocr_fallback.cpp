#include "rapid_ocr.h"

// Fallback build (no ONNX Runtime headers): links so Tools → OCR can explain
// the fetch step instead of failing to compile.
RapidOcrEngine::RapidOcrEngine(QObject *parent) : IOcrEngine(parent) {}
RapidOcrEngine::~RapidOcrEngine() = default;

bool RapidOcrEngine::available(QString *reason) {
  if (reason)
    *reason = "Built without ONNX Runtime - run scripts/fetch-ocr.ps1, "
              "reconfigure, rebuild.";
  return false;
}

QVector<QVector<NS::TextBlock>> RapidOcrEngine::recognize(const QStringList &,
                                                         QString *error) {
  if (error)
    *error = "OCR needs an ONNX Runtime build - run scripts/fetch-ocr.ps1, "
             "reconfigure, rebuild.";
  return {};
}
// NOTE: sortReadingOrder/ctcDecode live in rapid_ocr_units.cpp (always built).
