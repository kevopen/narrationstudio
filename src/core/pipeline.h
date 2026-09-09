#pragma once
#include "project.h"
#include <QDir>
#include <QStringList>
#include <functional>

// Import: PDF / folder / MangaDx / Webtoon -> temp/pages (or temp/<slug>).
// Improvement over Python: zero-copy QImageReader downscale happens in the
// worker (see ui/workers), this layer only resolves/normalizes paths.
namespace NS {
class Pipeline {
public:
  static QStringList collectImages(const QString &folder);   // png/jpg/jpeg/webp sorted
  static QString tempDirFor(const QString &slug);            // temp/<slug>
  static QString pagesDir();                                 // temp/pages
  static bool isPdf(const QString &path);
  static QString slugify(const QString &s);
  // Render every PDF page to PNG (long side = maxSide) under temp/pdf-<slug>/.
  // Runs in a worker thread; progress(done, total) pumps the status bar.
  static QStringList renderPdfPages(const QString &pdfPath, int maxSide = 2048,
                                    std::function<void(int,int)> progress = {},
                                    QString *error = nullptr);
};
} // namespace NS
