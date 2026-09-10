#include "video_service.h"
#include <atomic>
#include <numeric>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QMutex>
#include <QPainter>
#include <QProcess>
#include <QRegularExpression>
#include <QTextStream>
#include <QThreadPool>
#include <QtConcurrent>
#include <QtConcurrent>

VideoService::VideoService(const QString &ffmpegExe, QObject *parent)
  : QObject(parent),
    m_ffmpeg(ffmpegExe.isEmpty() ? resolveFfmpeg() : ffmpegExe) {}

QString VideoService::resolveFfmpeg() {
#ifdef Q_OS_WIN
  const QString exe = "ffmpeg.exe";
#else
  const QString exe = "ffmpeg";
#endif
  QStringList cands;
  const QString appDir = QCoreApplication::applicationDirPath();
  cands << QDir(appDir).absoluteFilePath(exe)
        << QDir(appDir + "/..").absoluteFilePath(exe)
        << QDir::current().absoluteFilePath(exe)
        << QDir::current().absoluteFilePath("third_party/ffmpeg/" + exe);
#ifdef NS_SOURCE_DIR
  cands << QString::fromLatin1(NS_SOURCE_DIR) + "/third_party/ffmpeg/" + exe;
#endif
  for (const QString &c : cands)
    if (QFile::exists(c)) return QDir::cleanPath(c);
  return exe; // PATH fallback (may fail with a clear message below)
}

bool VideoService::haveFfmpeg(QString *pathOut) {
  const QString p = resolveFfmpeg();
  if (QFile::exists(p)) {
    if (pathOut) *pathOut = p;
    return true;
  }
  // Bare "ffmpeg" relies on PATH — verify it actually runs.
  QProcess probe;
  probe.start(p, {"-version"});
  const bool runs = probe.waitForStarted(5000) && probe.waitForFinished(10000)
                    && probe.exitCode() == 0;
  if (runs && pathOut) *pathOut = p;
  return runs;
}

QImage VideoService::cover(const QImage &src, int w, int h) {
  if (src.isNull() || src.width() <= 0 || src.height() <= 0) {
    QImage blank(w, h, QImage::Format_RGB888); blank.fill(Qt::black); return blank;
  }
  double s = qMax(double(w) / src.width(), double(h) / src.height());
  QImage r = src.scaled(qMax(1, static_cast<int>(src.width()*s)), qMax(1, static_cast<int>(src.height()*s)),
                        Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  return r.copy((r.width() - w) / 2, (r.height() - h) / 2, w, h);
}

QImage VideoService::contain(const QImage &src, int w, int h) {
  return src.scaled(w, h, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

bool VideoService::renderFrame(const QString &srcPath, const QString &outPath,
                               int w, int h, const QString &bgMode, const QString &bgImage) {
  QImageReader rd(srcPath);
  rd.setAutoTransform(true);
  QImage page = rd.read();
  if (page.isNull()) return false;
  QImage canvas(w, h, QImage::Format_RGB888);
  if (bgMode == "blur") {
    QImage bg = cover(page, w, h);
    QImage small = bg.scaled(qMax(1, w/16), qMax(1, h/16), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    canvas = small.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  } else if (bgMode == "white") {
    canvas.fill(Qt::white);
  } else if (bgMode == "image" && !bgImage.isEmpty()) {
    QImage bg(bgImage);
    canvas = bg.isNull() ? QImage(w, h, QImage::Format_RGB888) : cover(bg, w, h);
    if (canvas.format() == QImage::Format_Invalid) canvas = QImage(w, h, QImage::Format_RGB888);
    if (canvas.isNull()) canvas.fill(Qt::black);
  } else {
    canvas.fill(Qt::black);
  }
  QImage fitted = contain(page.convertToFormat(QImage::Format_RGB888), w, h);
  QPainter p(&canvas);
  p.drawImage((w - fitted.width())/2, (h - fitted.height())/2, fitted);
  p.end();
  return canvas.save(outPath, "JPG", 92);
}

QString VideoService::exportSlideshow(const QStringList &images, const QString &audio,
                                      const QString &output, const QVector<double> &durations,
                                      const Options &opt, QString *error,
                                      std::function<void(int, const QString &)> progress,
                                      std::function<bool()> shouldStop) {
  if (images.isEmpty()) { if (error) *error = "No images to export."; return {}; }
  QVector<double> durs = durations;
  if (durs.isEmpty()) durs = QVector<double>(images.size(), opt.defaultDuration);
  if (durs.size() != images.size()) { if (error) *error = "durations mismatch"; return {}; }

  QDir tmp(QDir::current().absoluteFilePath("temp/frames"));
  tmp.mkpath(".");
  // 1. Pre-compose frames (parallel: each frame is independent; falls back
  // to sequential on single-core machines).
  QStringList frames;
  frames.reserve(images.size());
  for (int i = 0; i < images.size(); ++i)
    frames << tmp.absoluteFilePath(QString("frame_%1.jpg").arg(i + 1, 5, 10, QChar('0')));
  auto renderOne = [&](int i) -> QString {
    if (shouldStop && shouldStop()) return {};
    if (!renderFrame(images[i], frames[i], opt.width, opt.height,
                     opt.backgroundMode, opt.backgroundImage))
      return QString("frame render failed: ") + images[i];
    return {};
  };
  {
    const int threads = QThreadPool::globalInstance()->maxThreadCount();
    // Indexed parallel render with progress + cancellation:
    std::atomic<int> done{0};
    std::atomic<bool> stopFlag{false};
    QString errText;
    QMutex errMutex;
    auto worker = [&](int i) {
      if (stopFlag.load() || (shouldStop && shouldStop())) { stopFlag.store(true); return; }
      QString e = renderOne(i);
      if (!e.isEmpty()) {
        QMutexLocker lock(&errMutex);
        if (errText.isEmpty()) errText = e;
        stopFlag.store(true);
        return;
      }
      const int d = ++done;
      if (progress && (d % 25 == 0 || d == images.size()))
        progress(d * 70 / qMax(1, images.size()),
                 QString("Rendering frame %1/%2...").arg(d).arg(images.size()));
    };
    if (threads > 1) {
      // chunk indices across pool threads via QtConcurrent::map
      QVector<int> idx(images.size());
      std::iota(idx.begin(), idx.end(), 0);
      QtConcurrent::blockingMap(idx, worker);
    } else {
      for (int i = 0; i < images.size(); ++i) worker(i);
    }
    if (!errText.isEmpty()) { if (error) *error = errText; return {}; }
    if (stopFlag.load()) {
      if (error) *error = "Export cancelled during frame rendering.";
      return {};
    }
  }
  // 2. Concat demuxer list
  QString listPath = tmp.absoluteFilePath("concat.txt");
  QFile list(listPath);
  if (!list.open(QIODevice::WriteOnly | QIODevice::Text)) {
    if (error) *error = "cannot write concat list"; return {};
  }
  QTextStream ts(&list);
  for (int i = 0; i < frames.size(); ++i) {
    // Forward slashes: the concat demuxer treats backslashes as escapes.
    QString fp = frames[i];
    fp.replace('\\', '/');
    fp.replace("'", "'\\''");
    ts << "file '" << fp << "'\n";
    ts << "duration " << QString::number(qMax(0.1, durs[i]), 'f', 3) << "\n";
  }
  QString lastFp = frames.last();
  lastFp.replace('\\', '/');
  lastFp.replace("'", "'\\''");
  ts << "file '" << lastFp << "'\n"; // trailing file (ffmpeg concat quirk)
  list.close();

  // 3. ffmpeg (bundled; clear hint when it cannot start)
  if (progress) progress(85, "Encoding video with ffmpeg...");
  // Hold the last page until the narration ends (+ small tail), like Python:
  // otherwise -shortest would cut the audio when it runs past the pictures.
  if (!audio.isEmpty()) {
    QFile af(audio);
    if (af.open(QIODevice::ReadOnly) && af.size() > 44) {
      QByteArray hdr = af.read(44);
      const uchar *hp = reinterpret_cast<const uchar *>(hdr.constData());
      auto r32 = [&](int o){ return quint32(hp[o]) | (quint32(hp[o+1])<<8) | (quint32(hp[o+2])<<16) | (quint32(hp[o+3])<<24); };
      const quint32 rate = r32(24), bytes = r32(40);
      if (rate >= 1000 && bytes > 0) {
        const double audioSec = double(bytes) / 2 / rate;
        const double slideSec = std::accumulate(durs.begin(), durs.end(), 0.0);
        if (audioSec > slideSec)
          durs.last() += audioSec - slideSec + 0.5;
      }
    }
  }
  QStringList args = {"-y", "-hide_banner", "-loglevel", "error",
                      "-f", "concat", "-safe", "0", "-i", listPath};
  if (!audio.isEmpty()) args += {"-i", audio};
  args += {"-vf", QString("scale=%1:%2").arg(opt.width).arg(opt.height),
           // veryfast: static slides compress trivially; medium would take
           // hours on thousand-page chapters for no visible gain.
           "-c:v", "libx264", "-preset", "veryfast",
           "-pix_fmt", "yuv420p", "-r", "25"};
  if (!audio.isEmpty()) args += {"-c:a", "aac", "-b:a", "192k", "-shortest"};
  args += {"-movflags", "+faststart", output};
  QProcess proc;
  proc.start(m_ffmpeg, args);
  if (!proc.waitForStarted(15000)) {
    if (error) *error = "ffmpeg not found (" + m_ffmpeg +
      "). Run scripts/fetch-ffmpeg.ps1, or install ffmpeg on PATH.";
    return {};
  }
  // No arbitrary timeout (huge chapters must be allowed to finish — the old
  // 30-minute cap silently reported success on timeout, leaving a moov-less
  // file). Pump in 1s slices: live frame= progress + cooperative cancel.
  const int totalFrames = qMax(1, int(std::accumulate(durs.begin(), durs.end(), 0.0) * 25));
  QByteArray errTail;
  static const QRegularExpression frameRe("frame=\\s*(\\d+)");
  for (;;) {
    if (shouldStop && shouldStop()) {
      proc.kill();
      proc.waitForFinished(10000);
      if (error) *error = "Export cancelled during encoding.";
      return {};
    }
    if (proc.waitForFinished(1000)) break;
    const QByteArray chunk = proc.readAllStandardError();
    if (!chunk.isEmpty()) {
      errTail += chunk;
      if (errTail.size() > 8192) errTail = errTail.right(8192);
      int frame = -1;
      auto it = frameRe.globalMatch(QString::fromUtf8(chunk));
      while (it.hasNext()) frame = it.next().captured(1).toInt();
      if (frame >= 0 && progress)
        progress(80 + qMin(19, frame * 19 / totalFrames),
                 QString("Encoding frame %1 (~%2%)...").arg(frame).arg(frame * 100 / totalFrames));
    } else if (progress) {
      progress(81, "Encoding video with ffmpeg...");
    }
  }
  errTail += proc.readAllStandardError();
  if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
    if (error) *error = QString("ffmpeg failed (exit %1): %2")
      .arg(proc.exitCode()).arg(QString::fromUtf8(errTail).trimmed().right(1500));
    return {};
  }
  if (!QFile::exists(output)) {
    if (error) *error = "ffmpeg did not produce " + output;
    return {};
  }
  if (progress) progress(100, "Export done.");
  return output;
}

QStringList VideoService::sliceAudio(const QString &masterWav,
                                     const QVector<double> &markers,
                                     const QString &outDir) {
  QStringList result;
  if (!QFile::exists(masterWav)) return result;
  QDir().mkpath(outDir);
  const QString ffmpeg = resolveFfmpeg();
  const int count = markers.size() + 1;
  // Build time boundaries: [0, m0, m1, ..., mN-1, total]
  QFile f(masterWav);
  double total = 0;
  if (f.open(QIODevice::ReadOnly) && f.size() > 44) {
    QByteArray hdr = f.read(44);
    const uchar *p = reinterpret_cast<const uchar*>(hdr.constData());
    auto r32 = [&](int o){ return quint32(p[o])|(quint32(p[o+1])<<8)|(quint32(p[o+2])<<16)|(quint32(p[o+3])<<24); };
    const quint32 rate = r32(24), bytes = r32(40);
    if (rate > 0 && bytes > 0) total = bytes / (rate * 2.0); // mono 16-bit
  }
  QVector<double> bounds;
  bounds << 0.0;
  for (double m : markers) bounds << m;
  bounds << total;
  for (int i = 0; i < count; ++i) {
    const double start = bounds[i];
    const double end = (i < bounds.size() - 1) ? bounds[i + 1] : total;
    const QString out = QDir(outDir).absoluteFilePath(QString("page_%1.wav").arg(i + 1));
    QProcess proc;
    proc.start(ffmpeg, {"-y", "-i", masterWav,
                        "-ss", QString::number(start, 'f', 4),
                        "-to", QString::number(end, 'f', 4),
                        "-c", "copy", out});
    proc.waitForFinished(30000);
    if (proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0 && QFile::exists(out))
      result << out;
  }
  return result;
}

QString VideoService::mergeAudio(const QStringList &pageAudio,
                                 const QSet<int> &excluded,
                                 const QString &outPath) {
  if (pageAudio.isEmpty()) return {};
  const QString ffmpeg = resolveFfmpeg();
  const QString listFile = outPath + ".txt";
  QFile listF(listFile);
  if (!listF.open(QIODevice::WriteOnly | QIODevice::Text)) return {};
  QTextStream ts(&listF);
  for (int i = 0; i < pageAudio.size(); ++i) {
    if (excluded.contains(i)) continue;
    if (!QFile::exists(pageAudio[i])) continue;
    ts << "file '" << QDir::current().relativeFilePath(pageAudio[i]) << "'\n";
  }
  listF.close();
  QProcess proc;
  proc.start(ffmpeg, {"-y", "-f", "concat", "-safe", "0", "-i", listFile,
                      "-c", "copy", outPath});
  proc.waitForFinished(60000);
  QFile::remove(listFile);
  if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) return {};
  return QFile::exists(outPath) ? outPath : QString();
}
