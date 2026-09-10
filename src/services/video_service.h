#pragma once
#include <QImage>
#include <QObject>
#include <QStringList>
#include <QVector>
#include <functional>

// Video export: pre-compose temp/frames/frame_*.jpg via cover/contain/blur
// (QImage, no PIL), then ffmpeg concat demuxer + -shortest.
// Mirrors mangastudio/app/services/video_service.py.
class VideoService : public QObject {
  Q_OBJECT
public:
  explicit VideoService(const QString &ffmpegExe = {}, QObject *parent = nullptr);

  struct Options {
    int width = 1280, height = 720;
    QString backgroundMode = "black"; // black|white|blur|image
    QString backgroundImage;
    double defaultDuration = 5.0;
  };

  // Bundled binary first (third_party/ffmpeg, exe dir), PATH last.
  static QString resolveFfmpeg();
  static bool haveFfmpeg(QString *pathOut = nullptr);

  // Returns output path on success. progress(pct, stage) pumps the UI;
  // shouldStop() is polled so hour-long exports stay cancellable.
  // No arbitrary timeout: huge chapters must be allowed to finish.
  QString exportSlideshow(const QStringList &images, const QString &audio,
                          const QString &output, const QVector<double> &durations,
                          const Options &opt, QString *error = nullptr,
                          std::function<void(int, const QString &)> progress = {},
                          std::function<bool()> shouldStop = {});

  static QImage cover(const QImage &src, int w, int h);
  static QImage contain(const QImage &src, int w, int h);
  static bool renderFrame(const QString &srcPath, const QString &outPath,
                          int w, int h, const QString &bgMode, const QString &bgImage);

  // Slice a master WAV into per-page segments using markers.
  // Returns list of output WAV paths (one per page, 0-indexed).
  static QStringList sliceAudio(const QString &masterWav,
                                const QVector<double> &markers,
                                const QString &outDir);

  // Concatenate only non-excluded page WAVs into a new master.
  // pageAudio: per-page WAV paths (indexed 0..N-1)
  // excluded: set of page indices to skip
  // Returns output WAV path.
  static QString mergeAudio(const QStringList &pageAudio,
                            const QSet<int> &excluded,
                            const QString &outPath);

private:
  QString m_ffmpeg;
};
