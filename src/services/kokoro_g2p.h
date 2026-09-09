#pragma once
#include <QMap>
#include <QMutex>
#include <QString>

// espeak-ng grapheme-to-phoneme via libespeak-ng.dll loaded at runtime
// (no link dependency). Mirrors kokoro-onnx's phonemizer espeak backend:
// en-us voice, IPA output, whole-sentence call, then vocab filter.
// Serialized with a mutex: espeak holds process-global state.
class EspeakG2P {
public:
  EspeakG2P(const QString &libPath, const QString &dataPath);
  ~EspeakG2P();

  bool isReady() const { return m_ready; }
  QString error() const { return m_error; }

  // "Hello, world!" -> "həlˈoʊ, wˈɜːld!" (filtered to vocab below)
  QString phonemize(const QString &text);

  // Keep only chars the Kokoro vocab knows; collapse whitespace.
  static QString filterVocab(const QString &phonemes, const QMap<QString,int> &vocab);

private:
  QString m_error;
  bool m_ready = false;
  void *m_lib = nullptr;
  QMutex m_mutex;

  struct Api;
  Api *m_api = nullptr;
};
