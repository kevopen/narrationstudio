#include <QtTest>
#include "services/video_service.h"
#include <QDir>
#include <QImage>
#include <QPainter>

class TVideo : public QObject {
  Q_OBJECT
private slots:
  void slideshowSmoke() {
    // Needs ffmpeg (fetch-ffmpeg.ps1); SKIP without it.
    QString ff;
    if (!VideoService::haveFfmpeg(&ff)) QSKIP("ffmpeg not available");
    // 2 synthetic pages + 2s silence: full render + concat + mux path.
    QDir().mkpath("temp");
    QStringList imgs;
    for (int i = 0; i < 2; ++i) {
      QImage img(320, 480, QImage::Format_RGB888);
      img.fill(i ? Qt::darkRed : Qt::darkBlue);
      QPainter p(&img);
      p.setPen(Qt::white);
      p.drawText(img.rect(), Qt::AlignCenter, QString("Page %1").arg(i + 1));
      p.end();
      const QString fp = QDir::current().absoluteFilePath(
        QString("temp/vsmoke_%1.png").arg(i));
      QVERIFY(img.save(fp, "PNG"));
      imgs << fp;
    }
    // 2s silence wav via the same layout the app joins
    const QString wav = QDir::current().absoluteFilePath("temp/vsmoke.wav");
    {
      QFile f(wav);
      QVERIFY(f.open(QIODevice::WriteOnly));
      QByteArray hdr(44, 0);
      const quint32 bytes = 24000 * 2 * 2;
      auto w32 = [&](int o, quint32 v){
        hdr[o] = char(v & 0xff); hdr[o+1] = char((v>>8)&0xff);
        hdr[o+2] = char((v>>16)&0xff); hdr[o+3] = char((v>>24)&0xff); };
      memcpy(hdr.data(), "RIFF", 4); w32(4, 36 + bytes);
      memcpy(hdr.data() + 8, "WAVE", 4);
      memcpy(hdr.data() + 12, "fmt ", 4); w32(16, 16);
      hdr[20] = 1; hdr[22] = 1; w32(24, 24000); w32(28, 48000);
      hdr[32] = 2; hdr[34] = 16;
      memcpy(hdr.data() + 36, "data", 4); w32(40, bytes);
      f.write(hdr);
      f.write(QByteArray(int(bytes), 0));
    }
    VideoService vs;
    VideoService::Options opt;
    opt.width = 320; opt.height = 240;
    QString err;
    int progressSeen = 0;
    const QString out = QDir::current().absoluteFilePath("temp/vsmoke.mp4");
    QFile::remove(out);
    QString res = vs.exportSlideshow(imgs, wav, out, {1.0, 1.0}, opt, &err,
      [&](int, const QString &){ ++progressSeen; });
    QVERIFY2(!res.isEmpty(), qPrintable(err));
    QFileInfo fi(out);
    QVERIFY2(fi.size() > 2000, qPrintable(QString::number(fi.size())));
    QVERIFY(progressSeen > 0);
    // moov must exist (the 3000-page failure mode was a moov-less file)
    QFile f(out);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QVERIFY(f.readAll().contains("moov"));
  }
};

QTEST_MAIN(TVideo)
#include "t_video.moc"
