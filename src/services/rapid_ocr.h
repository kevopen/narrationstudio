#pragma once
#include "engines.h"

// English-only OCR (PP-OCRv3 DB detection + CRNN recognition, ONNX Runtime):
// detect → line-merge → batched recognize → CTC decode → reading order.
// Mirrors mangastudio PaddleOcrEngine batch predict([paths]) +
// sort_blocks_reading_order. Needs models/ocr/*.onnx; without them
// available() is false and recognize() explains how to fetch.
struct OcrPaths {
  QString detModel;   // ch_PP-OCRv3_det_infer.onnx
  QString recModel;   // ch_PP-OCRv3_rec_infer.onnx

  static OcrPaths resolve();
  bool complete(QStringList *missing = nullptr) const;
};

class RapidOcrEngine : public IOcrEngine {
  Q_OBJECT
public:
  explicit RapidOcrEngine(QObject *parent = nullptr);
  ~RapidOcrEngine() override;

  static bool available(QString *reason = nullptr);

  QVector<QVector<NS::TextBlock>> recognize(const QStringList &paths,
                                            QString *error) override;

  // Testable units (no models needed)
  static QVector<QRect> sortReadingOrder(QVector<QRect> boxes);
  // logits [seq, classes] for ONE line → (text, mean confidence)
  static QPair<QString,double> ctcDecode(const float *logits, int seqLen,
                                         int numClasses, const QStringList &keys);

private:
  struct Impl;
  Impl *m_impl = nullptr;
  bool ensureInit(QString *error);
};
