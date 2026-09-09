#include "pipeline.h"
#include <QDirIterator>
#include <QFileInfo>
#include <QPdfDocument>
#include <QRegularExpression>
#include <QStandardPaths>

namespace NS {

QStringList Pipeline::collectImages(const QString &folder) {
  QStringList out;
  QDirIterator it(folder, {"*.png","*.jpg","*.jpeg","*.webp"}, QDir::Files,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) out << it.next();
  std::sort(out.begin(), out.end(),
            [](const QString &a, const QString &b){
              return a.localeAwareCompare(b) < 0;
            });
  return out;
}

QString Pipeline::pagesDir() {
  QDir d(QDir::current());
  d.mkpath("temp/pages");
  return d.absoluteFilePath("temp/pages");
}

QString Pipeline::tempDirFor(const QString &slug) {
  QDir d(QDir::current());
  d.mkpath("temp/" + slug);
  return d.absoluteFilePath("temp/" + slug);
}

bool Pipeline::isPdf(const QString &path) {
  return path.endsWith(".pdf", Qt::CaseInsensitive);
}

QString Pipeline::slugify(const QString &s) {
  QString o = s.toLower().trimmed();
  o.replace(QRegularExpression("[^a-z0-9]+"), "-");
  o.replace(QRegularExpression("(^-|-$)"), "");
  return o.isEmpty() ? QStringLiteral("untitled") : o;
}

QStringList Pipeline::renderPdfPages(const QString &pdfPath, int maxSide,
                                     std::function<void(int,int)> progress,
                                     QString *error) {
  QPdfDocument doc;
  if (doc.load(pdfPath) != QPdfDocument::Error::None) {
    if (error) *error = "Cannot open PDF (encrypted or corrupt?).";
    return {};
  }
  const int n = doc.pageCount();
  if (n <= 0) {
    if (error) *error = "PDF has no pages.";
    return {};
  }
  const QString dir = tempDirFor("pdf-" + slugify(QFileInfo(pdfPath).baseName()));
  QStringList out;
  for (int i = 0; i < n; ++i) {
    const QSizeF pts = doc.pagePointSize(i);
    const double s = maxSide / qMax(pts.width(), pts.height());
    const QSize px(qMax(1, static_cast<int>(pts.width() * s)),
                   qMax(1, static_cast<int>(pts.height() * s)));
    const QImage img = doc.render(i, px);
    if (img.isNull()) continue;  // skip blank/broken page, keep numbering
    const QString fp = QDir(dir).absoluteFilePath(
      QString("page_%1.png").arg(i + 1, 5, 10, QChar('0')));
    if (!img.save(fp, "PNG")) {
      if (error) *error = "Cannot write " + fp;
      return {};
    }
    out << fp;
    if (progress) progress(i + 1, n);
  }
  if (out.isEmpty() && error) *error = "No pages rendered.";
  return out;
}

} // namespace NS
