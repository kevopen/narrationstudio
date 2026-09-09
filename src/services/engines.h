#pragma once
#include "core/project.h"
#include <QObject>
#include <QStringList>
#include <QVector>
#include <functional>

// ---- Pluggable AI interfaces: UI talks to these, never to torch/ONNX directly.
// Default build wires fast stubs; -DENABLE_AI_BACKENDS=ON plugs real sessions
// without touching UI. This is the main shareability + testability upgrade.

class IOcrEngine : public QObject {
  Q_OBJECT
public:
  using QObject::QObject;
  // Batch OCR over absolute page paths (active Work Range only).
  virtual QVector<QVector<NS::TextBlock>> recognize(const QStringList &paths, QString *error = nullptr) = 0;
};

class ICaptionEngine : public QObject {
  Q_OBJECT
public:
  using QObject::QObject;
  virtual QString describe(const QString &imagePath, QString *error = nullptr) = 0;
};

class INarrator : public QObject {
  Q_OBJECT
public:
  using QObject::QObject;
  virtual QString narrate(const QString &prompt, QString *error = nullptr) = 0;
};

class ITtsEngine : public QObject {
  Q_OBJECT
public:
  using QObject::QObject;
  struct Boundary { double seconds = 0; int charIndex = 0; };
  virtual bool synthesize(const QString &text, const QString &wavOut,
                          QVector<Boundary> *bounds = nullptr, QString *error = nullptr) = 0;
  // Per-page synthesis into wavDir/page_00001.wav... Default loops over
  // synthesize(); engines with costly init (Kokoro sessions) override it to
  // reuse one session and report per-page progress.
  virtual bool synthesizePages(const QStringList &texts, const QString &wavDir,
                               QStringList *wavOuts,
                               QVector<QVector<Boundary>> *boundsPerPage,
                               std::function<void(int,int)> progress,
                               QString *error = nullptr);
};

// ---- Stubs (compile + run with zero models) ----
class StubOcrEngine : public IOcrEngine {
  Q_OBJECT
public:
  using IOcrEngine::IOcrEngine;
  QVector<QVector<NS::TextBlock>> recognize(const QStringList &paths, QString *error = nullptr) override;
};
class StubCaptionEngine : public ICaptionEngine {
  Q_OBJECT
public:
  using ICaptionEngine::ICaptionEngine;
  QString describe(const QString &imagePath, QString *error = nullptr) override;
};
class StubNarrator : public INarrator {
  Q_OBJECT
public:
  using INarrator::INarrator;
  QString narrate(const QString &prompt, QString *error = nullptr) override;
};
class StubTtsEngine : public ITtsEngine {
  Q_OBJECT
public:
  using ITtsEngine::ITtsEngine;
  bool synthesize(const QString &text, const QString &wavOut,
                  QVector<Boundary> *bounds = nullptr, QString *error = nullptr) override;
};
