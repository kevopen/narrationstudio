#pragma once
#include "engines.h"

// Florence-2-base captioning (plain ONNX Runtime, int8 parts, no GenAI dep):
// 768px CLIP preprocess → vision encoder → prompt embed → encoder →
// autoregressive decoder with KV-cache past (16-wide windows).
// Task prompt is "<MORE_DETAILED_CAPTION>" (BPE-encoded like the reference).
// Needs models/florence/*.onnx + tokenizer.json; without them available()
// is false and describe() explains the fetch step.
struct FlorencePaths {
  QString vision, encoder, embed, decoder, decoderPast, tokenizer;

  static FlorencePaths resolve();
  bool complete(QStringList *missing = nullptr) const;
};

class FlorenceCaptionEngine : public ICaptionEngine {
  Q_OBJECT
public:
  explicit FlorenceCaptionEngine(QObject *parent = nullptr);
  ~FlorenceCaptionEngine() override;

  static bool available(QString *reason = nullptr);

  QString describe(const QString &imagePath, QString *error = nullptr) override;

private:
  struct Impl;
  Impl *m_impl = nullptr;
  bool ensureInit(QString *error);
};
