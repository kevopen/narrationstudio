#include "kokoro_engine.h"
#include "kokoro_npz.h"
#include "kokoro_voices.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
QStringList candidateRoots() {
  QStringList roots;
  const QString exe = QCoreApplication::applicationDirPath();
  roots << exe << exe + "/.." << exe + "/../share";
  roots << QDir::currentPath();
  // dev fallbacks
  roots << QDir::currentPath() + "/build/release";
#ifdef NS_SOURCE_DIR
  roots << QString::fromLatin1(NS_SOURCE_DIR);
#endif
  return roots;
}

QString findUnder(const QString &rel) {
  for (const QString &r : candidateRoots()) {
    const QString p = QDir(r).absoluteFilePath(rel);
    if (QFile::exists(p) || QDir(p).exists())
      return QDir::cleanPath(p);
  }
  return {};
}
} // namespace

KokoroPaths KokoroPaths::resolve() {
  KokoroPaths p;
  // Full-precision first (Python parity, best quality), int8 fallback.
  p.modelPath = findUnder("models/kokoro/kokoro-v1.0.onnx");
  if (p.modelPath.isEmpty())
    p.modelPath = findUnder("models/kokoro/kokoro-v1.0.int8.onnx");
  if (p.modelPath.isEmpty())
    p.modelPath = findUnder("third_party/kokoro/kokoro-v1.0.onnx");
  p.voicesPath = findUnder("models/kokoro/voices-v1.0.bin");
  if (p.voicesPath.isEmpty())
    p.voicesPath = findUnder("third_party/kokoro/voices-v1.0.bin");
  p.vocabPath = findUnder("models/kokoro/kokoro_vocab.json");
#ifdef Q_OS_WIN
  p.espeakLib = findUnder("espeak-ng.dll");
  if (p.espeakLib.isEmpty())
    p.espeakLib = findUnder("third_party/espeak-ng/espeak-ng.dll");
  // espeak-ng.dll from espeakng-loader is named espeak-ng.dll; the
  // distro zip calls it libespeak-ng.dll — accept either.
  if (p.espeakLib.isEmpty())
    p.espeakLib = findUnder("third_party/espeak-ng/libespeak-ng.dll");
  p.espeakData = findUnder("espeak-ng-data");
  if (p.espeakData.isEmpty() || !QDir(p.espeakData).exists())
    p.espeakData = findUnder("third_party/espeak-ng/espeak-ng-data");
#else
  p.espeakLib = findUnder("libespeak-ng.so");
  p.espeakData = findUnder("espeak-ng-data");
#endif
  return p;
}

bool KokoroPaths::complete(QStringList *missing) const {
  QStringList m;
  if (modelPath.isEmpty() || !QFile::exists(modelPath)) m << "kokoro model (.onnx)";
  if (voicesPath.isEmpty() || !QFile::exists(voicesPath)) m << "voices-v1.0.bin";
  if (espeakLib.isEmpty() || !QFile::exists(espeakLib)) m << "espeak-ng library";
  if (espeakData.isEmpty() || !QDir(espeakData).exists()) m << "espeak-ng-data";
  if (missing) *missing = m;
  return m.isEmpty();
}

bool loadKokoroVocab(const QString &overridePath, QMap<QString,int> *out,
                     QString *error) {
  QByteArray raw;
  if (!overridePath.isEmpty() && QFile::exists(overridePath)) {
    QFile f(overridePath);
    if (!f.open(QIODevice::ReadOnly)) {
      if (error) *error = "Cannot open " + overridePath;
      return false;
    }
    raw = f.readAll();
  } else {
    QFile f(":/kokoro_vocab.json");
    if (!f.open(QIODevice::ReadOnly)) {
      // dev fallback: resources dir next to binary/source
      const QString dev = findUnder("resources/kokoro_vocab.json");
      QFile g(dev);
      if (dev.isEmpty() || !g.open(QIODevice::ReadOnly)) {
        if (error) *error = "kokoro vocab not found";
        return false;
      }
      raw = g.readAll();
    } else {
      raw = f.readAll();
    }
  }
  const QJsonObject root = QJsonDocument::fromJson(raw).object();
  const QJsonObject vocab = root["vocab"].toObject();
  if (vocab.isEmpty()) {
    if (error) *error = "kokoro vocab is empty";
    return false;
  }
  QMap<QString,int> map;
  for (auto it = vocab.begin(); it != vocab.end(); ++it)
    map.insert(it.key(), it.value().toInt());
  *out = map;
  return true;
}
