#include "mangadx_service.h"
#include "core/pipeline.h"
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

MangaDxService::MangaDxService(QObject *parent) : QObject(parent) {}

static QByteArray getSync(const QUrl &url, QString *error, int ms = 30000) {
  QNetworkAccessManager nam;
  QNetworkRequest req(url);
  req.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);
  req.setRawHeader("User-Agent", "NarrationStudio/0.1");
  QNetworkReply *rep = nam.get(req);
  QEventLoop loop;
  QObject::connect(rep, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  QTimer t; t.setSingleShot(true);
  QObject::connect(&t, &QTimer::timeout, &loop, [&]{ if (rep->isRunning()) rep->abort(); });
  t.start(ms); loop.exec();
  if (rep->error() != QNetworkReply::NoError) {
    if (error) {
      *error = rep->errorString();
      if (rep->error() == QNetworkReply::HostNotFoundError)
        *error += " - api.mangadx.org does not resolve here (DNS block?). "
                  "Try DNS 1.1.1.1 / 8.8.8.8 or a VPN; the Python app needs it too.";
    }
    rep->deleteLater(); return {};
  }
  QByteArray d = rep->readAll(); rep->deleteLater(); return d;
}

static QVector<MangaDxService::Manga> parseMangaList(const QByteArray &data) {
  QVector<MangaDxService::Manga> out;
  if (data.isEmpty()) return out;
  for (const auto &it : QJsonDocument::fromJson(data).object()["data"].toArray()) {
    QJsonObject m = it.toObject();
    MangaDxService::Manga g; g.id = m["id"].toString();
    QJsonObject attrs = m["attributes"].toObject();
    auto titles = attrs["title"].toObject();
    g.title = titles["en"].toString(titles.begin().value().toString());
    for (const auto &rel : m["relationships"].toArray()) {
      if (rel.toObject()["type"].toString() == "cover_art") {
        QString fn = rel.toObject()["attributes"].toObject()["fileName"].toString();
        if (!fn.isEmpty()) g.coverUrl = QString("https://uploads.mangadx.org/covers/%1/%2.256.jpg").arg(g.id, fn);
      }
    }
    out.append(g);
  }
  return out;
}

QVector<MangaDxService::Manga> MangaDxService::search(const QString &query, int limit, QString *error) {
  QUrl url(QString("https://api.mangadx.org/manga?title=%1&limit=%2&includes[]=cover_art&order[relevance]=desc")
           .arg(QString::fromUtf8(QUrl::toPercentEncoding(query)), QString::number(limit)));
  return parseMangaList(getSync(url, error));
}

QVector<MangaDxService::Manga> MangaDxService::popular(int limit, QString *error) {
  QUrl url(QString("https://api.mangadx.org/manga?limit=%1&includes[]=cover_art&order[followedCount]=desc&contentRating[]=safe&contentRating[]=suggestive")
           .arg(limit));
  return parseMangaList(getSync(url, error));
}

QVector<MangaDxService::Manga> MangaDxService::latest(int limit, QString *error) {
  QUrl url(QString("https://api.mangadx.org/manga?limit=%1&includes[]=cover_art&order[latestUploadedChapter]=desc&contentRating[]=safe&contentRating[]=suggestive")
           .arg(limit));
  return parseMangaList(getSync(url, error));
}

QVector<MangaDxService::Chapter> MangaDxService::chapters(const QString &mangaId, QString *error) {
  QVector<Chapter> out; QString offset;
  for (int page = 0; page < 20; ++page) { // cap paging; UI diffs known_ids
    QUrl url(QString("https://api.mangadx.org/manga/%1/feed?limit=100&offset=%2&order[chapter]=asc&translatedLanguage[]=en")
             .arg(mangaId, offset.isEmpty() ? "0" : offset));
    QByteArray data = getSync(url, error);
    if (data.isEmpty()) break;
    QJsonObject o = QJsonDocument::fromJson(data).object();
    for (const auto &it : o["data"].toArray()) {
      QJsonObject c = it.toObject(), a = c["attributes"].toObject();
      Chapter ch; ch.id = c["id"].toString();
      ch.chapter = a["chapter"].toString(); ch.title = a["title"].toString();
      out.append(ch);
    }
    int total = o["total"].toInt(out.size());
    if (out.size() >= total) break;
    offset = QString::number(out.size());
  }
  return out;
}

QStringList MangaDxService::downloadChapter(const QString &mangaId, const QString &chapterId,
                                            const QString &slug, QString *error) {
  Q_UNUSED(mangaId);
  QByteArray data = getSync(QUrl("https://api.mangadx.org/at-home/server/" + chapterId), error);
  if (data.isEmpty()) return {};
  QJsonObject o = QJsonDocument::fromJson(data).object();
  QString base = o["baseUrl"].toString();
  QJsonObject ch = o["chapter"].toObject();
  QString hash = ch["hash"].toString();
  QStringList names; for (const auto &v : ch["data"].toArray()) names << v.toString();
  QString dir = NS::Pipeline::tempDirFor("mangadx-" + NS::Pipeline::slugify(slug));
  QDir().mkpath(dir);
  QStringList local;
  for (const auto &n : names) {
    QUrl u(QString("%1/data/%2/%3").arg(base, hash, n));
    QByteArray img = getSync(u, error, 60000);
    if (img.isEmpty()) continue;
    QString fp = QDir(dir).absoluteFilePath(n);
    QFile f(fp); f.open(QIODevice::WriteOnly); f.write(img); f.close();
    local << fp;
  }
  return local;
}
