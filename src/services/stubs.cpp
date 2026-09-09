#include "services/engines.h"
#include <QDir>
#include <QFile>

bool ITtsEngine::synthesizePages(const QStringList &texts, const QString &wavDir,
                                 QStringList *wavOuts,
                                 QVector<QVector<Boundary>> *boundsPerPage,
                                 std::function<void(int,int)> progress,
                                 QString *error) {
  QDir().mkpath(wavDir);
  for (int i = 0; i < texts.size(); ++i) {
    if (progress) progress(i, texts.size());
    const QString out = QDir(wavDir).absoluteFilePath(
      QString("page_%1.wav").arg(i + 1, 5, 10, QChar('0')));
    QVector<Boundary> b;
    if (!synthesize(texts[i], out, boundsPerPage ? &b : nullptr, error))
      return false;
    if (wavOuts) *wavOuts << out;
    if (boundsPerPage) *boundsPerPage << b;
  }
  if (progress) progress(texts.size(), texts.size());
  return true;
}

QVector<QVector<NS::TextBlock>> StubOcrEngine::recognize(const QStringList &paths, QString *) {
  QVector<QVector<NS::TextBlock>> out;
  for (const auto &p : paths) {
    Q_UNUSED(p);
    out.append(QVector<NS::TextBlock>{});   // real Paddle/ONNX engine plugs in here later
  }
  return out;
}

QString StubCaptionEngine::describe(const QString &, QString *) {
  return QStringLiteral("(describe this panel: stub - plug Florence-2 ONNX here)");
}

QString StubNarrator::narrate(const QString &prompt, QString *) {
  Q_UNUSED(prompt);
  return QStringLiteral("[PAGE 1]\n narration stub - connect Gemini/Ollama in Tools > Narrate.");
}

bool StubTtsEngine::synthesize(const QString &text, const QString &wavOut,
                               QVector<Boundary> *bounds, QString *) {
  // Write 1s of silence so timeline/export keep working with no models.
  const int rate = 24000;
  const int n = rate; // 1 second
  QByteArray pcm; pcm.resize(n * 2); pcm.fill(0);
  QFile f(wavOut);
  if (!f.open(QIODevice::WriteOnly)) return false;
  // minimal WAV header (mono 16-bit)
  QByteArray hdr; hdr.resize(44); hdr.fill(0);
  memcpy(hdr.data(), "RIFF", 4);
  auto w32 = [&](int off, quint32 v){ hdr[off]=v&0xff; hdr[off+1]=(v>>8)&0xff; hdr[off+2]=(v>>16)&0xff; hdr[off+3]=(v>>24)&0xff; };
  auto w16 = [&](int off, quint16 v){ hdr[off]=v&0xff; hdr[off+1]=(v>>8)&0xff; };
  w32(4, 36 + pcm.size()); memcpy(hdr.data()+8, "WAVE", 4);
  memcpy(hdr.data()+12, "fmt ", 4); w32(16, 16); w16(20, 1); w16(22, 1);
  w32(24, rate); w32(28, rate*2); w16(32, 2); w16(34, 16);
  memcpy(hdr.data()+36, "data", 4); w32(40, pcm.size());
  f.write(hdr); f.write(pcm);
  if (bounds) { bounds->clear(); bounds->append({0.0, 0}); bounds->append({1.0, (int)text.size()}); }
  return true;
}
