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

bool KokoroTtsEngine::synthesizePages(const QStringList &texts, const QString &wavDir,
                                      QStringList *wavOuts,
                                      QVector<QVector<Boundary>> *boundsPerPage,
                                      std::function<void(int,int)> progress,
                                      QString *error) {
  // Base-class loop over synthesize(); fails fast with the message above.
  return ITtsEngine::synthesizePages(texts, wavDir, wavOuts, boundsPerPage,
                                     progress, error);
}
