#include "florence_engine.h"
#include "florence_tok.h"
#include <onnxruntime_cxx_api.h>
#include <algorithm>
#include <cmath>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QThread>

namespace {
constexpr int FL_IMG = 768;
constexpr int FL_FEATS = 577;
constexpr int FL_HID = 768;
constexpr int FL_EOS = 2;
constexpr int FL_START = 2;
constexpr int FL_MAX_NEW = 128;
constexpr int FL_PAST_WIN = 16;

QStringList candidateRoots() {
  QStringList roots;
  const QString exe = QCoreApplication::applicationDirPath();
  roots << exe << exe + "/.." << exe + "/../share";
  roots << QDir::currentPath();
#ifdef NS_SOURCE_DIR
  roots << QString::fromLatin1(NS_SOURCE_DIR);
#endif
  return roots;
}

QString findUnder(const QString &rel) {
  for (const QString &r : candidateRoots()) {
    const QString p = QDir(r).absoluteFilePath(rel);
    if (QFile::exists(p)) return QDir::cleanPath(p);
  }
  return {};
}

Ort::Env &sharedEnv() {
  static Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "NarrationStudioFlorence"};
  return env;
}

// Greedy argmax with no-repeat-trigram blocking (generation default).
int pickNext(const float *logits, int vocab, const QVector<int> &gen) {
  std::vector<char> banned(vocab, 0);
  if (gen.size() >= 2) {
    const int a = gen[gen.size() - 2], b = gen[gen.size() - 1];
    for (qsizetype i = 0; i + 2 < gen.size(); ++i)
      if (gen[i] == a && gen[i + 1] == b && gen[i + 2] < vocab)
        banned[gen[i + 2]] = 1;
  }
  int best = 0;
  for (int i = 1; i < vocab; ++i) {
    if (banned[i]) continue;
    if (!banned[best] && logits[i] <= logits[best]) continue;
    if (logits[i] > logits[best] || banned[best]) best = i;
  }
  return best;
}
} // namespace

struct FlorenceCaptionEngine::Impl {
  Ort::Session vision{nullptr}, encoder{nullptr}, embed{nullptr};
  Ort::Session decoder{nullptr}, decoderPast{nullptr};
  FlorenceBpe tok;
  QVector<int> taskIds;
  bool ready = false;
};

FlorenceCaptionEngine::FlorenceCaptionEngine(QObject *parent)
  : ICaptionEngine(parent), m_impl(new Impl()) {}
FlorenceCaptionEngine::~FlorenceCaptionEngine() { delete m_impl; }

FlorencePaths FlorencePaths::resolve() {
  FlorencePaths p;
  const QString base = "models/florence/";
  p.vision = findUnder(base + "vision_encoder_int8.onnx");
  p.encoder = findUnder(base + "encoder_model_int8.onnx");
  p.embed = findUnder(base + "embed_tokens_int8.onnx");
  p.decoder = findUnder(base + "decoder_model_int8.onnx");
  p.decoderPast = findUnder(base + "decoder_with_past_model_int8.onnx");
  p.tokenizer = findUnder(base + "tokenizer.json");
  return p;
}

bool FlorencePaths::complete(QStringList *missing) const {
  QStringList m;
  if (vision.isEmpty()) m << "vision_encoder";
  if (encoder.isEmpty()) m << "encoder_model";
  if (embed.isEmpty()) m << "embed_tokens";
  if (decoder.isEmpty()) m << "decoder_model";
  if (decoderPast.isEmpty()) m << "decoder_with_past";
  if (tokenizer.isEmpty()) m << "tokenizer.json";
  if (missing) *missing = m;
  return m.isEmpty();
}

bool FlorenceCaptionEngine::available(QString *reason) {
  const FlorencePaths p = FlorencePaths::resolve();
  QStringList missing;
  if (!p.complete(&missing)) {
    if (reason)
      *reason = "Describe models missing: " + missing.join(", ")
              + " - run scripts/fetch-florence.ps1.";
    return false;
  }
  return true;
}

bool FlorenceCaptionEngine::ensureInit(QString *error) {
  if (m_impl->ready) return true;
  const FlorencePaths p = FlorencePaths::resolve();
  QStringList missing;
  if (!p.complete(&missing)) {
    if (error)
      *error = "Describe models missing: " + missing.join(", ")
             + " - run scripts/fetch-florence.ps1.";
    return false;
  }
  if (!m_impl->tok.load(p.tokenizer, error)) return false;
  // The task token is replaced by its sentence prompt, wrapped in
  // <s>...</s> (port of Florence2Processor._construct_prompts +
  // prepare_inputs_layout: image_token×577 + bos + prompt + eos).
  // add_special_tokens=False, so BOS/EOS are added manually as ids.
  m_impl->taskIds.clear();
  m_impl->taskIds << 0;
  m_impl->taskIds += m_impl->tok.encode(
    "Describe with a paragraph what is shown in the image.");
  m_impl->taskIds << 2;
  if (m_impl->taskIds.size() <= 2) {
    if (error) *error = "Task prompt produced no tokens.";
    return false;
  }
  try {
    Ort::SessionOptions opt;
    opt.SetIntraOpNumThreads(std::max(1, QThread::idealThreadCount() - 1));
    opt.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
#ifdef _WIN32
    auto load = [&](const QString &path) {
      return Ort::Session(sharedEnv(), path.toStdWString().c_str(), opt);
    };
#else
    auto load = [&](const QString &path) {
      return Ort::Session(sharedEnv(), path.toStdString().c_str(), opt);
    };
#endif
    m_impl->vision = load(p.vision);
    m_impl->encoder = load(p.encoder);
    m_impl->embed = load(p.embed);
    m_impl->decoder = load(p.decoder);
    m_impl->decoderPast = load(p.decoderPast);
  } catch (const Ort::Exception &e) {
    if (error) *error = "Describe session failed: " + QString::fromUtf8(e.what());
    return false;
  }
  m_impl->ready = true;
  return true;
}

QString FlorenceCaptionEngine::describe(const QString &imagePath, QString *error) {
  if (!ensureInit(error)) return {};

  QImageReader rd(imagePath);
  rd.setAutoTransform(true);
  QImage img = rd.read().convertToFormat(QImage::Format_RGB888);
  if (img.isNull()) {
    if (error) *error = "Cannot read image.";
    return {};
  }
  // CLIP preprocess: 768x768, /255, mean/std (per preprocessor_config.json)
  QImage small = img.scaled(FL_IMG, FL_IMG, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  const float mean[3] = {0.485f, 0.456f, 0.406f};
  const float stdv[3] = {0.229f, 0.224f, 0.225f};
  QVector<float> pix(static_cast<qsizetype>(3) * FL_IMG * FL_IMG);
  for (int y = 0; y < FL_IMG; ++y) {
    const uchar *row = small.constScanLine(y);
    for (int x = 0; x < FL_IMG; ++x)
      for (int c = 0; c < 3; ++c)
        pix[(static_cast<qsizetype>(c) * FL_IMG + y) * FL_IMG + x] =
          (row[x * 3 + c] / 255.0f - mean[c]) / stdv[c];
  }

  Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(
    OrtAllocatorType::OrtArenaAllocator, OrtMemType::OrtMemTypeDefault);
  const int T = m_impl->taskIds.size();
  const int S = FL_FEATS + T; // 577 image + prompt tokens

  try {
    // 1. vision → image_features [1,577,768]
    std::array<int64_t, 4> vsh = {1, 3, FL_IMG, FL_IMG};
    Ort::Value vt = Ort::Value::CreateTensor<float>(
      mem, pix.data(), static_cast<size_t>(pix.size()), vsh.data(), 4);
    const char *vin[1] = {"pixel_values"};
    const char *vout[1] = {"image_features"};
    Ort::Value vinA[1] = {std::move(vt)};
    auto vres = m_impl->vision.Run(Ort::RunOptions{nullptr}, vin, vinA, 1, vout, 1);
    float *feats = vres[0].GetTensorMutableData<float>();

    // 2. prompt ids → embeds [1,T,768]
    QVector<int64_t> tids;
    for (int id : m_impl->taskIds) tids << int64_t(id);
    std::array<int64_t, 2> tsh = {1, T};
    Ort::Value tt = Ort::Value::CreateTensor<int64_t>(
      mem, tids.data(), static_cast<size_t>(tids.size()), tsh.data(), 2);
    const char *ein[1] = {"input_ids"};
    const char *eout[1] = {"inputs_embeds"};
    Ort::Value einA[1] = {std::move(tt)};
    auto eres = m_impl->embed.Run(Ort::RunOptions{nullptr}, ein, einA, 1, eout, 1);
    float *pemb = eres[0].GetTensorMutableData<float>();

    // 3. encoder over [image | prompt] → last_hidden_state [1,S,768]
    QVector<float> encIn(static_cast<qsizetype>(S) * FL_HID);
    memcpy(encIn.data(), feats, static_cast<size_t>(FL_FEATS) * FL_HID * sizeof(float));
    memcpy(encIn.data() + static_cast<qsizetype>(FL_FEATS) * FL_HID, pemb,
           static_cast<size_t>(T) * FL_HID * sizeof(float));
    QVector<int64_t> encMask(S, 1);
    std::array<int64_t, 2> esh = {1, S};
    std::array<int64_t, 3> esh3 = {1, S, FL_HID};
    Ort::Value em1 = Ort::Value::CreateTensor<int64_t>(
      mem, encMask.data(), static_cast<size_t>(encMask.size()), esh.data(), 2);
    Ort::Value em2 = Ort::Value::CreateTensor<float>(
      mem, encIn.data(), static_cast<size_t>(encIn.size()), esh3.data(), 3);
    const char *ecin[2] = {"attention_mask", "inputs_embeds"};
    const char *ecout[1] = {"last_hidden_state"};
    Ort::Value ecinA[2] = {std::move(em1), std::move(em2)};
    auto encRes = m_impl->encoder.Run(Ort::RunOptions{nullptr}, ecin, ecinA, 2, ecout, 1);
    float *encHid = encRes[0].GetTensorMutableData<float>();
    const qsizetype encCount = static_cast<qsizetype>(S) * FL_HID;
    QVector<float> encKeep(encCount);
    memcpy(encKeep.data(), encHid, static_cast<size_t>(encCount) * sizeof(float));

    // one token id → [1,1,768] embedding (appended to the running prefix)
    auto embedOne = [&](int id, QVector<float> &out) {
      int64_t one[1] = {id};
      std::array<int64_t, 2> osh = {1, 1};
      Ort::Value ot = Ort::Value::CreateTensor<int64_t>(
        mem, one, 1, osh.data(), 2);
      const char *inN[1] = {"input_ids"};
      const char *ouN[1] = {"inputs_embeds"};
      Ort::Value inA[1] = {std::move(ot)};
      auto r = m_impl->embed.Run(Ort::RunOptions{nullptr}, inN, inA, 1, ouN, 1);
      float *e = r[0].GetTensorMutableData<float>();
      const qsizetype at = out.size();
      out.resize(at + FL_HID);
      memcpy(out.data() + at, e, FL_HID * sizeof(float));
    };

    // past store: 6 layers × {decoder K/V (trimmed), encoder K/V (static)}
    struct Past { QVector<float> decK[6], decV[6], encK[6], encV[6]; int decLen = 0; };
    Past past;
    QVector<float> decEmb; // running decoder prefix embeddings ([start, gen...])
    embedOne(FL_START, decEmb);

    QVector<int> gen; // generated ids (without the start token)
    for (int step = 0; step < FL_MAX_NEW; ++step) {
      QVector<float> logits;
      const int D = static_cast<int>(decEmb.size() / FL_HID); // prefix length
      if (D <= FL_PAST_WIN) {
        // full decoder over the whole prefix (short: exact, no past mgmt)
        std::array<int64_t, 3> dsh = {1, D, FL_HID};
        Ort::Value dt = Ort::Value::CreateTensor<float>(
          mem, decEmb.data(), static_cast<size_t>(decEmb.size()), dsh.data(), 3);
        std::array<int64_t, 2> msh = {1, S};
        Ort::Value mt = Ort::Value::CreateTensor<int64_t>(
          mem, encMask.data(), static_cast<size_t>(encMask.size()), msh.data(), 2);
        std::array<int64_t, 3> hsh = {1, S, FL_HID};
        Ort::Value ht = Ort::Value::CreateTensor<float>(
          mem, encKeep.data(), static_cast<size_t>(encKeep.size()), hsh.data(), 3);
        const char *inN[3] = {"encoder_attention_mask", "encoder_hidden_states", "inputs_embeds"};
        Ort::Value inA[3] = {std::move(mt), std::move(ht), std::move(dt)};
        Ort::AllocatorWithDefaultOptions alloc;
        const size_t nout = m_impl->decoder.GetOutputCount();
        std::vector<std::string> onames;
        std::vector<const char *> on;
        for (size_t i = 0; i < nout; ++i) {
          char *n = m_impl->decoder.GetOutputNameAllocated(i, alloc).release();
          onames.emplace_back(n ? n : "");
          alloc.Free(n);
        }
        for (auto &s : onames) on.push_back(s.c_str());
        auto out = m_impl->decoder.Run(Ort::RunOptions{nullptr}, inN, inA, 3,
                                       on.data(), on.size());
        float *lg = out[0].GetTensorMutableData<float>();
        auto li = out[0].GetTensorTypeAndShapeInfo().GetShape();
        const int V = static_cast<int>(li.back());
        logits.resize(V);
        for (int v = 0; v < V; ++v)
          logits[v] = lg[static_cast<qsizetype>(D - 1) * V + v];
        // stash KV: outputs are logits, then per layer decK,decV,encK,encV
        for (int l = 0; l < 6; ++l) {
          const size_t base = 1 + static_cast<size_t>(l) * 4;
          if (base + 3 >= out.size()) break;
          auto grab = [&](size_t i) {
            auto si = out[i].GetTensorTypeAndShapeInfo().GetShape();
            qsizetype n = 1;
            for (auto d : si) n *= static_cast<qsizetype>(d);
            float *d = out[i].GetTensorMutableData<float>();
            return QVector<float>(d, d + n);
          };
          past.decK[l] = grab(base); past.decV[l] = grab(base + 1);
          past.encK[l] = grab(base + 2); past.encV[l] = grab(base + 3);
        }
        past.decLen = D;
      } else {
        // past window: decoder KV trimmed to first (D-16), feed final 16
        const int keep = D - FL_PAST_WIN;
        Ort::AllocatorWithDefaultOptions alloc;
        const size_t npin = m_impl->decoderPast.GetInputCount();
        std::vector<std::string> pinNames;
        for (size_t i = 0; i < npin; ++i) {
          char *n = m_impl->decoderPast.GetInputNameAllocated(i, alloc).release();
          pinNames.emplace_back(n ? n : "");
          alloc.Free(n);
        }
        std::vector<Ort::Value> inVals;
        std::vector<const char *> inNames;
        std::vector<QVector<float>> keepers; // trimmed KV lifetime
        keepers.reserve(12);
        for (size_t i = 0; i < npin; ++i) {
          const std::string &nm = pinNames[i];
          if (nm == "encoder_attention_mask") {
            std::array<int64_t, 2> msh = {1, S};
            inVals.push_back(Ort::Value::CreateTensor<int64_t>(
              mem, encMask.data(), static_cast<size_t>(encMask.size()),
              msh.data(), 2));
          } else if (nm == "inputs_embeds") {
            const qsizetype tailAt =
              (static_cast<qsizetype>(D) - FL_PAST_WIN) * FL_HID;
            std::array<int64_t, 3> wsh = {1, FL_PAST_WIN, FL_HID};
            inVals.push_back(Ort::Value::CreateTensor<float>(
              mem, decEmb.data() + tailAt,
              static_cast<size_t>(FL_PAST_WIN) * FL_HID, wsh.data(), 3));
          } else {
            // past_key_values.{l}.{decoder|encoder}.{key|value}, l in 0..5
            const int layer = (nm.size() > 16 && nm[16] >= '0' && nm[16] <= '5')
                              ? nm[16] - '0' : -1;
            const bool isDec = nm.find(".decoder.") != std::string::npos;
            const bool isKey = !nm.empty() && nm.back() == 'y';
            QVector<float> t;
            if (layer >= 0) {
              const QVector<float> *src = isDec
                ? (isKey ? &past.decK[layer] : &past.decV[layer])
                : (isKey ? &past.encK[layer] : &past.encV[layer]);
              if (isDec) {
                // [1,12,decLen,64] → first `keep`
                t.resize(static_cast<qsizetype>(12) * keep * 64);
                for (int h = 0; h < 12; ++h)
                  memcpy(t.data() + static_cast<qsizetype>(h) * keep * 64,
                         src->constData() + static_cast<qsizetype>(h) * past.decLen * 64,
                         static_cast<size_t>(keep) * 64 * sizeof(float));
              } else {
                t = *src; // encoder KV static
              }
            }
            keepers.push_back(t);
            const QVector<float> &k = keepers.back();
            std::array<int64_t, 4> tsh = {1, 12,
              isDec ? int64_t(keep) : int64_t(S), 64};
            inVals.push_back(Ort::Value::CreateTensor<float>(
              mem, const_cast<float *>(k.constData()),
              static_cast<size_t>(k.size()), tsh.data(), 4));
          }
          inNames.push_back(pinNames[i].c_str());
        }
        const size_t nout = m_impl->decoderPast.GetOutputCount();
        std::vector<std::string> ponNames;
        for (size_t i = 0; i < nout; ++i) {
          char *n = m_impl->decoderPast.GetOutputNameAllocated(i, alloc).release();
          ponNames.emplace_back(n ? n : "");
          alloc.Free(n);
        }
        std::vector<const char *> on;
        for (auto &s : ponNames) on.push_back(s.c_str());
        auto out = m_impl->decoderPast.Run(Ort::RunOptions{nullptr},
                                           inNames.data(), inVals.data(),
                                           inVals.size(), on.data(), on.size());
        float *lg = out[0].GetTensorMutableData<float>();
        auto li = out[0].GetTensorTypeAndShapeInfo().GetShape();
        const int V = static_cast<int>(li.back());
        logits.resize(V);
        const qsizetype last = static_cast<qsizetype>(FL_PAST_WIN - 1) * V;
        for (int v = 0; v < V; ++v) logits[v] = lg[last + v];
        // refresh decoder KV from this call's presents (12 decoder tensors)
        for (size_t i = 1; i < out.size(); ++i) {
          auto si = out[i].GetTensorTypeAndShapeInfo().GetShape();
          qsizetype n = 1;
          for (auto d : si) n *= static_cast<qsizetype>(d);
          float *d = out[i].GetTensorMutableData<float>();
          const int layer = static_cast<int>((i - 1) / 2);
          const bool isKey = ((i - 1) % 2 == 0);
          if (layer < 6) {
            QVector<float> &dst = isKey ? past.decK[layer] : past.decV[layer];
            dst.resize(n);
            memcpy(dst.data(), d, static_cast<size_t>(n) * sizeof(float));
          }
        }
        past.decLen = D;
      }
      const int next = pickNext(logits.constData(), logits.size(), gen);
      if (next == FL_EOS) break;
      gen.append(next);
      embedOne(next, decEmb); // extend prefix for the next step
    }
    return m_impl->tok.decode(gen);
  } catch (const Ort::Exception &e) {
    if (error) *error = "Describe inference failed: " + QString::fromUtf8(e.what());
    return {};
  }
}
