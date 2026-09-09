#pragma once
#include "engines.h"
#include <QMap>

// Kokoro TTS voice (libtorch-free ONNX port of kokoro-onnx v1.0):
// sentence-chunked synthesis, espeak-ng G2P, NPZ voice styles,
// measured per-sentence boundaries for the timeline.
// Needs third_party/onnxruntime + models/kokoro at build/run time;
// without them available() is false and synthesize() explains what's missing.
struct KokoroPaths {
  QString modelPath;   // kokoro-v1.0.int8.onnx (fp32 accepted too)
  QString voicesPath;  // voices-v1.0.bin (npz)
  QString vocabPath;   // kokoro_vocab.json override (empty = use :/ resource)
  QString espeakLib;   // espeak-ng.dll / libespeak-ng.so
  QString espeakData;  // espeak-ng-data dir

  static KokoroPaths resolve();
  bool complete(QStringList *missing = nullptr) const;
};

class KokoroTtsEngine : public ITtsEngine {
  Q_OBJECT
public:
  KokoroTtsEngine(const QString &voice, double speed, QObject *parent = nullptr);
  ~KokoroTtsEngine() override;

  // True when this build has ORT and all runtime files resolve.
  static bool available(QString *reason = nullptr);
  // Voice names from voices file, or the embedded v1.0 list.
  static QStringList voices(QString *error = nullptr);

  bool synthesize(const QString &text, const QString &wavOut,
                  QVector<Boundary> *bounds, QString *error) override;
  bool synthesizePages(const QStringList &texts, const QString &wavDir,
                       QStringList *wavOuts,
                       QVector<QVector<Boundary>> *boundsPerPage,
                       std::function<void(int,int)> progress,
                       QString *error) override;

private:
  struct Impl;
  Impl *m_impl = nullptr;
  QString m_voice;
  double m_speed = 1.0;
};

bool loadKokoroVocab(const QString &overridePath, QMap<QString,int> *out,
                     QString *error = nullptr);
