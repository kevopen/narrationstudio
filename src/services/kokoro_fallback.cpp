#include "kokoro_engine.h"
#include "kokoro_voices.h"
#include "services/engines.h"

// Fallback build (no ONNX Runtime headers): the class still links so the UI
// can offer the Voice dialog + downloader; synthesis explains the rebuild.
KokoroTtsEngine::KokoroTtsEngine(const QString &voice, double speed, QObject *parent)
  : ITtsEngine(parent), m_voice(voice), m_speed(speed) {}

KokoroTtsEngine::~KokoroTtsEngine() = default;

bool KokoroTtsEngine::available(QString *reason) {
  if (reason)
    *reason = "Built without ONNX Runtime - reinstall with Kokoro enabled, "
              "or run scripts/fetch-kokoro.ps1 then rebuild.";
  return false;
}

QStringList KokoroTtsEngine::voices(QString *) {
  return kokoroDefaultVoices();
}

bool KokoroTtsEngine::synthesize(const QString &, const QString &,
                                 QVector<Boundary> *, QString *error) {
  if (error)
    *error = "Kokoro needs an ONNX Runtime build - run "
             "scripts/fetch-kokoro.ps1, reconfigure, rebuild.";
  return false;
}
