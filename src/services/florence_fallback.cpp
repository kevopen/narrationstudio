#include "florence_engine.h"

// Fallback build (no ONNX Runtime headers): links so Tools → Describe can
// explain the fetch step instead of failing to compile.
FlorenceCaptionEngine::FlorenceCaptionEngine(QObject *parent)
  : ICaptionEngine(parent) {}
FlorenceCaptionEngine::~FlorenceCaptionEngine() = default;

bool FlorenceCaptionEngine::available(QString *reason) {
  if (reason)
    *reason = "Built without ONNX Runtime - run scripts/fetch-florence.ps1, "
              "reconfigure, rebuild.";
  return false;
}

QString FlorenceCaptionEngine::describe(const QString &, QString *error) {
  if (error)
    *error = "Describe needs an ONNX Runtime build - run "
             "scripts/fetch-florence.ps1, reconfigure, rebuild.";
  return {};
}
