#include "rapid_ocr.h"
#include <onnxruntime_cxx_api.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <queue>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QThread>

namespace {
// PP-OCRv3 EN-tuned constants (from rapidocr_onnxruntime config.yaml)
constexpr double DET_THRESH = 0.3;
constexpr double DET_BOX_THRESH = 0.5;
constexpr int DET_LIMIT_SIDE = 736;
constexpr int DET_MAX_SIDE = 4096;
constexpr int REC_H = 48;
constexpr int REC_MAX_W = 2048;
constexpr int REC_BATCH = 6;
// Keep threshold low: the narration prompt explicitly tolerates OCR noise
// ("infer the intended words"), so recall beats precision here.
constexpr double TEXT_SCORE = 0.35;

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
  static Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "NarrationStudioOCR"};
  return env;
}

int ortThreads() { return std::max(1, QThread::idealThreadCount() - 1); }

std::string ioName(Ort::Session &s, Ort::AllocatorWithDefaultOptions &alloc,
                   size_t i, bool input) {
  char *n = input ? s.GetInputNameAllocated(i, alloc).release()
                  : s.GetOutputNameAllocated(i, alloc).release();
  std::string out = n ? n : "";
  alloc.Free(n);
  return out;
}

// Connected components (8-conn BFS) on a binary mask → AABBs in mask coords.
struct Comp { int x0, y0, x1, y1; int area; double score; };

QVector<Comp> findComponents(const float *prob, int w, int h, double thresh) {
  QVector<Comp> comps;
  QVector<char> seen(static_cast<qsizetype>(w) * h, 0);
  QVector<int> stack;
  const qsizetype W = w;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const qsizetype at = static_cast<qsizetype>(y) * w + x;
      if (seen[at] || prob[at] <= thresh) continue;
      int x0 = x, y0 = y, x1 = x, y1 = y, area = 0;
      double psum = 0;
      stack.clear(); stack.append(static_cast<int>(at)); seen[at] = 1;
      while (!stack.isEmpty()) {
        const int cur = stack.takeLast();
        const int cx = cur % w, cy = cur / w;
        ++area; psum += prob[cur];
        x0 = std::min(x0, cx); y0 = std::min(y0, cy);
        x1 = std::max(x1, cx); y1 = std::max(y1, cy);
        for (int dy = -1; dy <= 1; ++dy) {
          for (int dx = -1; dx <= 1; ++dx) {
            if (!dx && !dy) continue;
            const int nx = cx + dx, ny = cy + dy;
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            const qsizetype n2 = static_cast<qsizetype>(ny) * w + nx;
            if (!seen[n2] && prob[n2] > thresh) {
              seen[n2] = 1; stack.append(static_cast<int>(n2));
            }
          }
        }
      }
      if (area >= 24)
        comps.append({x0, y0, x1, y1, area, psum / area});
    }
  }
  return comps;
}

// 3x3 dilate on float mask (use_dilation=true in baseline config)
void dilate3(QVector<float> &m, int w, int h) {
  QVector<float> src = m;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      float v = 0;
      for (int dy = -1; dy <= 1 && v <= 0; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
          const int nx = x + dx, ny = y + dy;
          if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
          v = std::max(v, src[static_cast<qsizetype>(ny) * w + nx]);
        }
      m[static_cast<qsizetype>(y) * w + x] = v;
    }
}
} // namespace

struct RapidOcrEngine::Impl {
  Ort::Session det{nullptr};
  Ort::Session rec{nullptr};
  std::string detIn, detOut, recIn, recOut;
  QStringList keys; // metadata lines; decode idx i>=1 → keys[i-1] or ' '
  bool ready = false;
};

RapidOcrEngine::RapidOcrEngine(QObject *parent)
  : IOcrEngine(parent), m_impl(new Impl()) {}
RapidOcrEngine::~RapidOcrEngine() { delete m_impl; }

OcrPaths OcrPaths::resolve() {
  OcrPaths p;
  p.detModel = findUnder("models/ocr/ch_PP-OCRv3_det_infer.onnx");
  p.recModel = findUnder("models/ocr/ch_PP-OCRv3_rec_infer.onnx");
  return p;
}

bool OcrPaths::complete(QStringList *missing) const {
  QStringList m;
  if (detModel.isEmpty() || !QFile::exists(detModel)) m << "det model (.onnx)";
  if (recModel.isEmpty() || !QFile::exists(recModel)) m << "rec model (.onnx)";
  if (missing) *missing = m;
  return m.isEmpty();
}

bool RapidOcrEngine::available(QString *reason) {
  const OcrPaths p = OcrPaths::resolve();
  QStringList missing;
  if (!p.complete(&missing)) {
    if (reason)
      *reason = "OCR models missing: " + missing.join(", ")
              + " - run scripts/fetch-ocr.ps1.";
    return false;
  }
  return true;
}

bool RapidOcrEngine::ensureInit(QString *error) {
  if (m_impl->ready) return true;
  const OcrPaths paths = OcrPaths::resolve();
  QStringList missing;
  if (!paths.complete(&missing)) {
    if (error)
      *error = "OCR models missing: " + missing.join(", ")
             + " - run scripts/fetch-ocr.ps1.";
    return false;
  }
  try {
    Ort::SessionOptions opt;
    opt.SetIntraOpNumThreads(ortThreads());
    opt.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    Ort::AllocatorWithDefaultOptions alloc;
#ifdef _WIN32
    m_impl->det = Ort::Session(sharedEnv(), paths.detModel.toStdWString().c_str(), opt);
    m_impl->rec = Ort::Session(sharedEnv(), paths.recModel.toStdWString().c_str(), opt);
#else
    m_impl->det = Ort::Session(sharedEnv(), paths.detModel.toStdString().c_str(), opt);
    m_impl->rec = Ort::Session(sharedEnv(), paths.recModel.toStdString().c_str(), opt);
#endif
    m_impl->detIn = ioName(m_impl->det, alloc, 0, true);
    m_impl->detOut = ioName(m_impl->det, alloc, 0, false);
    m_impl->recIn = ioName(m_impl->rec, alloc, 0, true);
    m_impl->recOut = ioName(m_impl->rec, alloc, 0, false);
    // Character list embedded in rec export metadata ("character" key)
    Ort::ModelMetadata meta = m_impl->rec.GetModelMetadata();
    auto metaKeys = meta.GetCustomMetadataMapKeysAllocated(alloc);
    QStringList lines;
    for (const auto &kptr : metaKeys) {
      const std::string k = kptr ? kptr.get() : "";
      if (k != "character") continue;
      auto vptr = meta.LookupCustomMetadataMapAllocated(k.c_str(), alloc);
      const QString all = QString::fromUtf8(vptr ? vptr.get() : "");
      for (const QString &ln : all.split('\n')) {
        QString t = ln;
        if (t.endsWith('\r')) t.chop(1);
        lines << t;
      }
      break;
    }
    // drop a single trailing empty line from the final newline
    while (!lines.isEmpty() && lines.last().isEmpty()) lines.takeLast();
    if (lines.isEmpty()) {
      if (error) *error = "Rec model has no embedded character list.";
      return false;
    }
    m_impl->keys = lines;
  } catch (const Ort::Exception &e) {
    if (error) *error = "OCR session failed: " + QString::fromUtf8(e.what());
    return false;
  }
  m_impl->ready = true;
  return true;
}

QVector<QVector<NS::TextBlock>> RapidOcrEngine::recognize(const QStringList &paths,
                                                         QString *error) {
  QVector<QVector<NS::TextBlock>> all;
  all.reserve(paths.size());
  if (!ensureInit(error)) return all;

  Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(
    OrtAllocatorType::OrtArenaAllocator, OrtMemType::OrtMemTypeDefault);
  const float mean[3] = {0.485f, 0.456f, 0.406f};
  const float stdv[3] = {0.229f, 0.224f, 0.225f};

  for (const QString &path : paths) {
    QVector<NS::TextBlock> page;
    QImageReader rd(path);
    rd.setAutoTransform(true);
    QImage img = rd.read().convertToFormat(QImage::Format_RGB888);
    if (img.isNull()) { all.append(page); continue; }
    const int ow = img.width(), oh = img.height();

    // --- detect: min side → 736, round /32, cap max 4096
    double ratio = DET_LIMIT_SIDE / double(std::min(ow, oh));
    int dw = static_cast<int>(std::round(ow * ratio));
    int dh = static_cast<int>(std::round(oh * ratio));
    if (std::max(dw, dh) > DET_MAX_SIDE) {
      const double c = DET_MAX_SIDE / double(std::max(dw, dh));
      dw = static_cast<int>(std::round(dw * c));
      dh = static_cast<int>(std::round(dh * c));
    }
    dw = (dw + 31) / 32 * 32; dh = (dh + 31) / 32 * 32;
    ratio = double(dw) / ow; // x-ratio (uniform scale kept)
    QImage small = img.scaled(dw, dh, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    QVector<float> detIn(static_cast<qsizetype>(3) * dh * dw);
    for (int y = 0; y < dh; ++y) {
      const uchar *row = small.constScanLine(y);
      for (int x = 0; x < dw; ++x) {
        const qsizetype px = static_cast<qsizetype>(y) * dw + x;
        for (int c = 0; c < 3; ++c)
          detIn[(static_cast<qsizetype>(c) * dh + y) * dw + x] =
            (row[x * 3 + c] / 255.0f - mean[c]) / stdv[c];
      }
    }
    QVector<QRect> lines;
    try {
      std::array<int64_t, 4> sh = {1, 3, dh, dw};
      Ort::Value t = Ort::Value::CreateTensor<float>(
        mem, detIn.data(), static_cast<size_t>(detIn.size()), sh.data(), 4);
      const char *inN[1] = {m_impl->detIn.c_str()};
      const char *outN[1] = {m_impl->detOut.c_str()};
      Ort::Value inArr[1] = {std::move(t)};
      auto out = m_impl->det.Run(Ort::RunOptions{nullptr}, inN, inArr, 1, outN, 1);
      float *prob = out[0].GetTensorMutableData<float>();
      auto sinfo = out[0].GetTensorTypeAndShapeInfo();
      auto shp = sinfo.GetShape(); // [1,1,oh,ow]
      const int ow2 = static_cast<int>(shp[3]), oh2 = static_cast<int>(shp[2]);
      QVector<float> mask(static_cast<qsizetype>(oh2) * ow2);
      for (qsizetype i = 0; i < mask.size(); ++i)
        mask[i] = prob[i] > DET_THRESH ? prob[i] : 0.0f;
      dilate3(mask, ow2, oh2);
      QVector<Comp> comps = findComponents(mask.constData(), ow2, oh2, DET_THRESH);
      // Anisotropic pad (not uniform scale): dialogue glyphs need generous
      // vertical margin or caps/ascenders clip and T/H/E misread. In det px:
      // x ±(15% + 4), y ±(45% + 4). Port of unclip_ratio intent for flat text.
      const double sx = double(ow) / ow2, sy = double(oh) / oh2;
      QVector<QRect> boxes;
      for (const Comp &c : comps) {
        if (c.score < DET_BOX_THRESH) continue;
        const double cw = c.x1 - c.x0 + 1, ch = c.y1 - c.y0 + 1;
        const double px = cw * 0.15 + 4, py = ch * 0.45 + 4;
        QRectF r((c.x0 - px) * sx, (c.y0 - py) * sy,
                 (cw + 2 * px) * sx, (ch + 2 * py) * sy);
        QRect ri = r.intersected(QRectF(0, 0, ow, oh)).toRect();
        if (ri.width() >= 8 && ri.height() >= 8) boxes.append(ri);
      }
      // merge into text lines (overlap → same row; small x-gap → join)
      std::sort(boxes.begin(), boxes.end(),
                [](const QRect &a, const QRect &b){ return a.top() < b.top(); });
      struct Row { QVector<QRect> items; };
      QVector<Row> rows;
      for (const QRect &b : boxes) {
        bool placed = false;
        for (Row &r : rows) {
          const QRect &f = r.items.first();
          const int overlap = std::min(b.bottom(), f.bottom())
                            - std::max(b.top(), f.top());
          if (overlap >= static_cast<int>(0.4 * std::min(b.height(), f.height()))) {
            r.items.append(b); placed = true; break;
          }
        }
        if (!placed) rows.append({QVector<QRect>{b}});
      }
      for (Row &r : rows) {
        std::sort(r.items.begin(), r.items.end(),
                  [](const QRect &a, const QRect &b){ return a.left() < b.left(); });
        QRect cur = r.items.first();
        const int avgH = cur.height();
        for (qsizetype i = 1; i < r.items.size(); ++i) {
          const int gap = r.items[i].left() - cur.right();
          if (gap <= static_cast<int>(0.8 * avgH)) {
            cur = cur.united(r.items[i]);
          } else {
            lines.append(cur.adjusted(-4, -2, 4, 2)
                         .intersected(QRect(0, 0, ow - 1, oh - 1)));
            cur = r.items[i];
          }
        }
        lines.append(cur.adjusted(-4, -2, 4, 2)
                     .intersected(QRect(0, 0, ow - 1, oh - 1)));
      }
      lines = sortReadingOrder(lines);
    } catch (const Ort::Exception &e) {
      if (error) *error = "OCR detect failed: " + QString::fromUtf8(e.what());
      return all;
    }
    if (lines.isEmpty()) { all.append(page); continue; }

    // --- recognize: h=48, batch ≤6 by aspect, pad to batch max width
    struct Crop { QImage img; QRect box; int order; };
    QVector<Crop> crops;
    for (qsizetype i = 0; i < lines.size(); ++i) {
      QImage c = img.copy(lines[i]);
      const int w2 = std::min(REC_MAX_W,
        std::max(16, static_cast<int>(std::ceil(REC_H * double(c.width()) / std::max(1, c.height())))));
      crops.append({c.scaled(w2, REC_H, Qt::IgnoreAspectRatio, Qt::SmoothTransformation),
                    lines[i], static_cast<int>(i)});
    }
    std::sort(crops.begin(), crops.end(), [](const Crop &a, const Crop &b){
      return double(a.img.width()) / a.img.height()
           < double(b.img.width()) / b.img.height();
    });
    struct Res { QString text; double conf; int order; };
    QVector<Res> results;
    try {
      for (qsizetype b0 = 0; b0 < crops.size(); b0 += REC_BATCH) {
        const qsizetype b1 = std::min<qsizetype>(crops.size(), b0 + REC_BATCH);
        int maxW = 0;
        for (qsizetype i = b0; i < b1; ++i)
          maxW = std::max(maxW, crops[i].img.width());
        const qsizetype B = b1 - b0;
        QVector<float> rin(B * 3 * REC_H * maxW, 0.0f);
        for (qsizetype i = 0; i < B; ++i) {
          const QImage &c = crops[b0 + i].img;
          for (int y = 0; y < REC_H; ++y) {
            const uchar *row = c.constScanLine(y);
            for (int x = 0; x < c.width(); ++x)
              for (int ch = 0; ch < 3; ++ch)
                rin[((i * 3 + ch) * REC_H + y) * maxW + x] =
                  (row[x * 3 + ch] / 255.0f - 0.5f) / 0.5f;
          }
        }
        std::array<int64_t, 4> sh = {B, 3, REC_H, maxW};
        Ort::Value t = Ort::Value::CreateTensor<float>(
          mem, rin.data(), static_cast<size_t>(rin.size()), sh.data(), 4);
        const char *inN[1] = {m_impl->recIn.c_str()};
        const char *outN[1] = {m_impl->recOut.c_str()};
        Ort::Value inArr[1] = {std::move(t)};
        auto out = m_impl->rec.Run(Ort::RunOptions{nullptr}, inN, inArr, 1, outN, 1);
        float *logits = out[0].GetTensorMutableData<float>();
        auto sinfo = out[0].GetTensorTypeAndShapeInfo();
        auto shp = sinfo.GetShape();
        // layout [B,S,C] (rapidocr) or [S,B,C] (paddle) — adapt
        const int C = static_cast<int>(shp.back());
        bool batchFirst = (shp.size() == 3 && shp[0] == B);
        const int S = static_cast<int>(batchFirst ? shp[1] : shp[0]);
        const int NB = static_cast<int>(batchFirst ? shp[0] : shp[1]);
        for (int b = 0; b < NB && b < B; ++b) {
          QVector<float> seq(static_cast<qsizetype>(S) * C);
          for (int s = 0; s < S; ++s)
            for (int c = 0; c < C; ++c)
              seq[static_cast<qsizetype>(s) * C + c] = batchFirst
                ? logits[(static_cast<qsizetype>(b) * S + s) * C + c]
                : logits[(static_cast<qsizetype>(s) * NB + b) * C + c];
          auto [txt, conf] = ctcDecode(seq.constData(), S, C, m_impl->keys);
          results.append({txt, conf, crops[b0 + b].order});
        }
      }
    } catch (const Ort::Exception &e) {
      if (error) *error = "OCR recognize failed: " + QString::fromUtf8(e.what());
      return all;
    }
    std::sort(results.begin(), results.end(),
              [](const Res &a, const Res &b){ return a.order < b.order; });
    for (const Res &r : results) {
      if (r.text.trimmed().isEmpty() || r.conf < TEXT_SCORE) continue;
      NS::TextBlock b;
      b.content = r.text;
      b.type = "dialogue";
      b.rect = lines[r.order];
      page.append(b);
    }
    all.append(page);
  }
  return all;
}
