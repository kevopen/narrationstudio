#pragma once
#include <QObject>
// Webtoon: QProcess wrapper around `webtoon-downloader` (same tool as
// Python) with chapter-range support: latest-only, or start..end chapters
// (end <= 0 = up to the newest). Native QNetwork port later.
class WebtoonService : public QObject {
  Q_OBJECT
public:
  explicit WebtoonService(QObject *parent = nullptr);
  QStringList download(const QString &urlOrSlug, const QString &outDir,
                       int startChapter = 1, int endChapter = 0,
                       bool latestOnly = true, QString *error = nullptr);
  // Back-compat: latest chapter only.
  QStringList downloadLatest(const QString &urlOrSlug, const QString &outDir,
                             QString *error = nullptr) {
    return download(urlOrSlug, outDir, 1, 0, true, error);
  }
};
