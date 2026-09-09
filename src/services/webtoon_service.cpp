#include "webtoon_service.h"
#include "core/pipeline.h"
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

WebtoonService::WebtoonService(QObject *parent) : QObject(parent) {}

static QString findDownloader() {
  // PATH first, then the uv-tool location (often missing from PATH).
  QString exe = QStandardPaths::findExecutable("webtoon-downloader");
  if (!exe.isEmpty()) return exe;
  const QString home = QDir::homePath();
  for (const QString &c : {home + "/.local/bin/webtoon-downloader.exe",
                           home + "/.local/bin/webtoon-downloader"}) {
    if (QFile::exists(c)) return c;
  }
  return {};
}

// Rewrite any pasted Webtoons URL into the series list URL the CLI accepts
// (it needs .../list?title_no=...). Mirrors Python's normalize_url.
static QString normalizeUrl(const QString &url, QString *error) {
  const QString u = url.trimmed();
  static const QRegularExpression titleNo("[?&]title_no=(\\d+)");
  auto m = titleNo.match(u);
  if (!m.hasMatch()) {
    if (error) *error = "Could not find 'title_no' in URL: " + url;
    return {};
  }
  const QString no = m.captured(1);
  QString base = u.split('?').first();
  if (base.contains("/list")) {
    base = base.left(base.indexOf("/list") + 5);
  } else {
    base = base;
    static const QRegularExpression viewer("/[^/]+/viewer/?$");
    if (base.contains(viewer))
      base.replace(viewer, "/list");
    else
      base = base.endsWith('/') ? base + "list" : base + "/list";
  }
  return base + "?title_no=" + no;
}

QStringList WebtoonService::download(const QString &urlOrSlug, const QString &outDir,
                                     int startChapter, int endChapter,
                                     bool latestOnly, QString *error) {
  const QString exe = findDownloader();
  if (exe.isEmpty()) {
    if (error)
      *error = "webtoon-downloader not found. Install it with: uv tool install webtoon_downloader";
    return {};
  }
  const QString url = normalizeUrl(urlOrSlug, error);
  if (url.isEmpty()) return {};
  QString dir = outDir.isEmpty()
      ? NS::Pipeline::tempDirFor("webtoon-" + NS::Pipeline::slugify(url)) : outDir;
  QDir().mkpath(dir);
  // Only files already here are old — the download must produce NEW ones.
  QSet<QString> before;
  for (const QString &p : NS::Pipeline::collectImages(dir)) before.insert(p);
  QStringList args;
  if (latestOnly) {
    args << "--latest";
  } else {
    args << "--start" << QString::number(qMax(1, startChapter));
    if (endChapter > 0) args << "--end" << QString::number(endChapter);
  }
  args << url << "--out" << dir << "--image-format" << "jpg" << "--save-as" << "images";
  QProcess p;
  p.start(exe, args);
  if (!p.waitForStarted(5000)) {
    if (error) *error = "Could not start webtoon-downloader.";
    return {};
  }
  p.waitForFinished(1000 * 60 * 30);
  if (p.exitCode() != 0) {
    if (error) {
      *error = "webtoon-downloader failed: " +
               QString::fromUtf8(p.readAllStandardError()).trimmed().right(1000);
      if (error->isEmpty()) *error = "webtoon-downloader failed (no details).";
    }
    return {};
  }
  QStringList fresh;
  for (const QString &img : NS::Pipeline::collectImages(dir))
    if (!before.contains(img)) fresh << img;
  if (fresh.isEmpty()) {
    if (error) *error = "Download finished but no new images found.";
    return {};
  }
  return fresh;
}
