#pragma once
#include <QNetworkAccessManager>
#include <QObject>

// MangaDx over QNetworkAccessManager (HTTP/2 keep-alive + retries).
// Mirrors mangastudio/app/services/mangadx_service.py.
class MangaDxService : public QObject {
  Q_OBJECT
public:
  struct Manga { QString id, title, coverUrl; };
  struct Chapter { QString id, chapter, title; QStringList pageUrls; };

  explicit MangaDxService(QObject *parent = nullptr);

  // Paged search; cover cache -> temp/mangadx/.covers.
  QVector<Manga> search(const QString &query, int limit = 20, QString *error = nullptr);
  // Browse lists for the dialog landing (Popular/Latest like Python).
  QVector<Manga> popular(int limit = 20, QString *error = nullptr);
  QVector<Manga> latest(int limit = 20, QString *error = nullptr);
  QVector<Chapter> chapters(const QString &mangaId, QString *error = nullptr);
  // Bulk download pages via keep-alive; returns local file paths.
  QStringList downloadChapter(const QString &mangaId, const QString &chapterId,
                              const QString &slug, QString *error = nullptr);
};
