#include "kokoro_engine.h"
#include "core/timeline.h"
#include "kokoro_g2p.h"
#include "kokoro_npz.h"
#include "kokoro_voices.h"
#include "misaki_lite.h"
#include <onnxruntime_cxx_api.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <QDir>
#include <QFile>
#include <QThread>

namespace {
constexpr int KOKORO_RATE = 24000;
constexpr int KOKORO_STYLE_DIM = 256;
constexpr double KOKORO_GAP_SEC = 0.12;

bool writeWavF32(const QString &path, const QVector<float> &pcm, QString *error) {
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly)) {
    if (error) *error = "Cannot write " + path;
    return false;
  }
  QByteArray hdr(44, 0);
  auto w32 = [&](int off, quint32 v) {
    hdr[off] = static_cast<char>(v & 0xff);
    hdr[off + 1] = static_cast<char>((v >> 8) & 0xff);
    hdr[off + 2] = static_cast<char>((v >> 16) & 0xff);
    hdr[off + 3] = static_cast<char>((v >> 24) & 0xff);
  };
  auto w16 = [&](int off, quint16 v) {
    hdr[off] = static_cast<char>(v & 0xff);
    hdr[off + 1] = static_cast<char>((v >> 8) & 0xff);
  };
  const quint32 bytes = static_cast<quint32>(pcm.size()) * 2;
  memcpy(hdr.data(), "RIFF", 4);
  w32(4, 36 + bytes); memcpy(hdr.data() + 8, "WAVE", 4);
  memcpy(hdr.data() + 12, "fmt ", 4); w32(16, 16); w16(20, 1); w16(22, 1);
  w32(24, KOKORO_RATE); w32(28, KOKORO_RATE * 2); w16(32, 2); w16(34, 16);
  memcpy(hdr.data() + 36, "data", 4); w32(40, bytes);
  f.write(hdr);
  QByteArray out;
  out.resize(static_cast<qsizetype>(pcm.size()) * 2);
  qint16 *dst = reinterpret_cast<qint16 *>(out.data());
  for (qsizetype i = 0; i < pcm.size(); ++i) {
    const float v = std::max(-1.0f, std::min(1.0f, pcm[i]));
    dst[i] = static_cast<qint16>(std::lround(v * 32767.0f));
  }
  f.write(out);
  return true;
}

// Drop leading/trailing near-silence (model pads batch ends), keeping a
// natural breath margin, then fade edges to zero so joins never click.
QVector<float> trimEdges(const QVector<float> &in) {
  constexpr float thresh = 0.015f;
  constexpr qsizetype margin = 960; // 40ms @24k
  qsizetype head = 0, tail = in.size();
  while (head < tail && std::fabs(in[head]) < thresh) ++head;
  while (tail > head && std::fabs(in[tail - 1]) < thresh) --tail;
  head = head > margin ? head - margin : 0;
  tail = std::min<qsizetype>(in.size(), tail + margin);
  if (tail <= head) return in;
  QVector<float> out = in.mid(head, tail - head);
  const qsizetype f = qMin<qsizetype>(200, out.size() / 2); // ~8ms fades
  for (qsizetype i = 0; i < f; ++i) {
    const float g = float(i) / float(qMax<qsizetype>(1, f));
    out[i] *= g;
    out[out.size() - 1 - i] *= g;
  }
  return out;
}
} // namespace

struct KokoroTtsEngine::Impl {
  Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "NarrationStudio"};
  Ort::Session session{nullptr};
  std::string tokensInput = "tokens";
  std::string audioOutput = "audio";
  bool speedIsInt = false;
  bool ready = false;
  QString error;
  // Reused across pages (one session per narration run, not per page).
  QMap<QString,int> vocab;
  NpzVoices::VoiceTensor voice;
  std::unique_ptr<EspeakG2P> g2p;
  MisakiLite misaki; // lexicon-first (misaki port); espeak only for unknowns
  double speed = 1.0;
  Ort::MemoryInfo mem{nullptr};

  bool ensureReady(const QString &voiceName, double spd, QString *error);
  bool synthText(const QString &text, QVector<float> &pcm,
                 QVector<ITtsEngine::Boundary> &marks, QString *error);
};

KokoroTtsEngine::KokoroTtsEngine(const QString &voice, double speed, QObject *parent)
  : ITtsEngine(parent), m_impl(new Impl()), m_voice(voice) {
  m_speed = std::max(0.5, std::min(2.0, speed));
}

KokoroTtsEngine::~KokoroTtsEngine() { delete m_impl; }

bool KokoroTtsEngine::available(QString *reason) {
  const KokoroPaths p = KokoroPaths::resolve();
  QStringList missing;
  if (!p.complete(&missing)) {
    if (reason)
      *reason = "Kokoro files missing: " + missing.join(", ")
              + " - Edit > Voice > Download.";
    return false;
  }
  return true;
}

QStringList KokoroTtsEngine::voices(QString *error) {
  const KokoroPaths p = KokoroPaths::resolve();
  if (!p.voicesPath.isEmpty() && QFile::exists(p.voicesPath)) {
    NpzVoices npz;
    if (npz.load(p.voicesPath, error))
      return npz.names();
  }
  if (error && error->isEmpty()) *error = "";
  return kokoroDefaultVoices();
}

bool KokoroTtsEngine::Impl::ensureReady(const QString &voiceName, double spd,
                                          QString *error) {
  if (ready && speed == spd) return true;
  ready = false;
  speed = spd;
  const KokoroPaths paths = KokoroPaths::resolve();
  QStringList missing;
  if (!paths.complete(&missing)) {
    if (error)
      *error = "Kokoro files missing: " + missing.join(", ")
             + " - Edit > Voice > Download.";
    return false;
  }
  if (!loadKokoroVocab(paths.vocabPath, &vocab, error)) return false;
  NpzVoices npz;
  if (!npz.load(paths.voicesPath, error)) return false;
  const QString voice = voiceName.isEmpty() ? QStringLiteral("af_heart") : voiceName;
  if (!npz.extract(voice, &this->voice, error)) return false;
  if (this->voice.cols != KOKORO_STYLE_DIM || this->voice.rows < 1) {
    if (error) *error = "Unexpected voice tensor for " + voice;
    return false;
  }
  g2p = std::make_unique<EspeakG2P>(paths.espeakLib, paths.espeakData);
  if (!g2p->isReady()) {
    if (error) *error = "espeak-ng failed: " + g2p->error();
    return false;
  }
  // Lexicon-first G2P (misaki port): curated stress for known words,
  // espeak fallback for the rest. Non-fatal if lexicons are missing —
  // synthesis falls back to pure espeak like before.
  if (misaki.load(MisakiLite::dataDir(), nullptr))
    misaki.setFallback(g2p.get());
  try {
    Ort::SessionOptions opt;
    const int threads = std::max(1, QThread::idealThreadCount() - 1);
    opt.SetIntraOpNumThreads(threads);
    opt.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
#ifdef _WIN32
    const std::wstring modelW = paths.modelPath.toStdWString();
    session = Ort::Session(env, modelW.c_str(), opt);
#else
    const std::string modelA = paths.modelPath.toStdString();
    session = Ort::Session(env, modelA.c_str(), opt);
#endif
    Ort::AllocatorWithDefaultOptions alloc;
    size_t nin = session.GetInputCount();
    bool hasInputIds = false;
    for (size_t i = 0; i < nin; ++i) {
      char *n = session.GetInputNameAllocated(i, alloc).release();
      const std::string s = n ? n : "";
      alloc.Free(n);
      if (s == "input_ids") hasInputIds = true;
    }
    tokensInput = hasInputIds ? "input_ids" : "tokens";
    if (session.GetOutputCount() > 0) {
      char *on = session.GetOutputNameAllocated(0, alloc).release();
      if (on && *on) audioOutput = on;
      alloc.Free(on);
    }
    for (size_t i = 0; i < nin; ++i) {
      char *n = session.GetInputNameAllocated(i, alloc).release();
      const std::string s = n ? n : "";
      alloc.Free(n);
      if (s == "speed") {
        Ort::TypeInfo ti = session.GetInputTypeInfo(i);
        auto elem = ti.GetTensorTypeAndShapeInfo().GetElementType();
        speedIsInt = (elem == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64);
      }
    }
    mem = Ort::MemoryInfo::CreateCpu(
      OrtAllocatorType::OrtArenaAllocator, OrtMemType::OrtMemTypeDefault);
  } catch (const Ort::Exception &e) {
    if (error) *error = "ONNX session failed: " + QString::fromUtf8(e.what());
    return false;
  }
  ready = true;
  return true;
}

bool KokoroTtsEngine::Impl::synthText(const QString &text, QVector<float> &pcm,
                                      QVector<ITtsEngine::Boundary> &marks,
                                      QString *error) {
  // Sentence-chunked synthesis with measured boundaries (karaoke-grade).
  auto ranges = NS::Timeline::splitSentences(text);
  if (ranges.isEmpty() && !text.trimmed().isEmpty()) ranges.append({0, text.size()});

  pcm.clear();
  marks.clear();
  const qsizetype gap = static_cast<qsizetype>(KOKORO_GAP_SEC * KOKORO_RATE);

  for (qsizetype si = 0; si < ranges.size(); ++si) {
    const QString sentence = text.mid(ranges[si].first,
                                      ranges[si].second - ranges[si].first);
    if (sentence.trimmed().isEmpty()) continue;
    // Chunk guard: model caps at 510 phonemes; split long sentences at spaces.
    // Lexicon-first phonemes (misaki port) when available, else raw espeak.
    QStringList pieces;
    {
      const QString ph = misaki.isLoaded()
        ? misaki.phonemize(sentence)
        : g2p->phonemize(sentence);
      QString rest = EspeakG2P::filterVocab(ph, vocab);
      while (rest.size() > 480) {
        int cut = rest.lastIndexOf(' ', 480);
        if (cut <= 0) cut = 480;
        pieces << rest.left(cut);
        rest = rest.mid(cut + 1);
      }
      if (!rest.isEmpty()) pieces << rest;
    }
    marks.append({pcm.isEmpty() ? 0.0 : double(pcm.size()) / KOKORO_RATE,
                  ranges[si].first});
    for (const QString &piece : pieces) {
      QVector<int64_t> ids;
      for (const QChar ch : piece) {
        auto it = vocab.find(QString(ch));
        if (it != vocab.end()) ids.append(static_cast<int64_t>(it.value()));
      }
      if (ids.isEmpty()) {
        pcm.resize(pcm.size() + KOKORO_RATE / 8); // 0.125s pause
        continue;
      }
      // Style row follows token count (port of kokoro-onnx _style_for)
      const int row = std::min<int>(static_cast<int>(ids.size()), voice.rows) - 1;
      const float *styleRow = voice.data.constData()
        + static_cast<qsizetype>(row) * KOKORO_STYLE_DIM;

      QVector<int64_t> withPads;
      withPads.reserve(ids.size() + 2);
      withPads.append(0);
      withPads += ids;
      withPads.append(0);

      try {
        std::array<int64_t, 2> tokShape = {1, withPads.size()};
        std::array<int64_t, 2> styleShape = {1, KOKORO_STYLE_DIM};
        std::array<int64_t, 1> speedShape = {1};
        Ort::Value tokT = Ort::Value::CreateTensor<int64_t>(
          mem, withPads.data(), static_cast<size_t>(withPads.size()),
          tokShape.data(), tokShape.size());
        Ort::Value styT = Ort::Value::CreateTensor<float>(
          mem, const_cast<float *>(styleRow),
          static_cast<size_t>(KOKORO_STYLE_DIM),
          styleShape.data(), styleShape.size());
        const float speedF = static_cast<float>(speed);
        const int64_t speedI = static_cast<int64_t>(std::lround(speed));
        Ort::Value spdT = speedIsInt
          ? Ort::Value::CreateTensor<int64_t>(mem, const_cast<int64_t *>(&speedI), 1,
                                              speedShape.data(), speedShape.size())
          : Ort::Value::CreateTensor<float>(mem, const_cast<float *>(&speedF), 1,
                                            speedShape.data(), speedShape.size());
        const char *names[3] = {tokensInput.c_str(), "style", "speed"};
        Ort::Value inputs[3] = {std::move(tokT), std::move(styT), std::move(spdT)};
        const char *outs[1] = {audioOutput.c_str()};
        auto out = session.Run(Ort::RunOptions{nullptr},
                               names, inputs, 3, outs, 1);
        float *audio = out[0].GetTensorMutableData<float>();
        auto shapeInfo = out[0].GetTensorTypeAndShapeInfo();
        qsizetype count = 1;
        for (auto d : shapeInfo.GetShape()) count *= static_cast<qsizetype>(d);
        QVector<float> chunk(audio, audio + count);
        chunk = trimEdges(chunk);
        pcm += chunk;
      } catch (const Ort::Exception &e) {
        if (error)
          *error = "Kokoro inference failed: " + QString::fromUtf8(e.what());
        return false;
      }
    }
    pcm.resize(pcm.size() + gap);
  }
  if (pcm.isEmpty()) {
    if (error) *error = "Nothing synthesized.";
    return false;
  }
  return true;
}

bool KokoroTtsEngine::synthesize(const QString &text, const QString &wavOut,
                                 QVector<Boundary> *bounds, QString *error) {
  if (!m_impl->ensureReady(m_voice, m_speed, error)) return false;
  QVector<float> pcm;
  QVector<Boundary> marks;
  if (!m_impl->synthText(text, pcm, marks, error)) return false;
  if (!writeWavF32(wavOut, pcm, error)) return false;
  if (bounds) {
    *bounds = marks;
    bounds->append({double(pcm.size()) / KOKORO_RATE, static_cast<int>(text.size())});
  }
  return true;
}

bool KokoroTtsEngine::synthesizePages(const QStringList &texts, const QString &wavDir,
                                      QStringList *wavOuts,
                                      QVector<QVector<Boundary>> *boundsPerPage,
                                      std::function<void(int,int)> progress,
                                      QString *error) {
  if (!m_impl->ensureReady(m_voice, m_speed, error)) return false;
  QDir().mkpath(wavDir);
  for (int i = 0; i < texts.size(); ++i) {
    if (progress) progress(i, texts.size());
    QVector<float> pcm;
    QVector<Boundary> marks;
    if (texts[i].trimmed().isEmpty()) {
      pcm.resize(24000 / 2); // empty page: half-second pause, keeps numbering
      marks.append({0.0, 0});
    } else {
      if (!m_impl->synthText(texts[i], pcm, marks, error)) return false;
      pcm.resize(pcm.size() + 24000 * 15 / 100); // 0.15s breath at page end
    }
    const QString out = QDir(wavDir).absoluteFilePath(
      QString("page_%1.wav").arg(i + 1, 5, 10, QChar('0')));
    if (!writeWavF32(out, pcm, error)) return false;
    if (wavOuts) *wavOuts << out;
    if (boundsPerPage) {
      marks.append({double(pcm.size()) / KOKORO_RATE, static_cast<int>(texts[i].size())});
      *boundsPerPage << marks;
    }
  }
  if (progress) progress(texts.size(), texts.size());
  return true;
}
