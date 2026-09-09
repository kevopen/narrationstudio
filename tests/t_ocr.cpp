#include <QtTest>
#include <QImage>
#include <QPainter>
#include "services/rapid_ocr.h"

class TOcr : public QObject {
  Q_OBJECT
private slots:
  void ctc() {
    // keys: idx1->A, idx2->B ; frames: A A blank B B(dup) → "AB"
    const QStringList keys = {"A", "B"};
    const int S = 5, C = 4;
    float L[S * C] = {};
    auto set = [&](int s, int c, float v){ L[s * C + c] = v; };
    set(0, 1, 0.9f); set(1, 1, 0.8f); set(2, 0, 0.9f);
    set(3, 2, 0.7f); set(4, 2, 0.6f);
    auto [text, conf] = RapidOcrEngine::ctcDecode(L, S, C, keys);
    QCOMPARE(text, QString("AB"));
    QVERIFY(conf > 0.79 && conf < 0.81);
  }

  void readingOrder() {
    QVector<QRect> boxes = {
      QRect(200, 10, 80, 24),   // row 1 right
      QRect(20, 12, 80, 24),    // row 1 left
      QRect(20, 200, 80, 24),   // row 2
    };
    auto out = RapidOcrEngine::sortReadingOrder(boxes);
    QCOMPARE(out.size(), 3);
    QCOMPARE(out[0].left(), 20);
    QCOMPARE(out[0].top(), 12);
    QCOMPARE(out[1].left(), 200);
    QCOMPARE(out[2].top(), 200);
  }

  void probe() {
    // NS_OCR_PROBE=<image> → dump blocks; empty env = skip.
    const QString path = QString::fromLocal8Bit(qgetenv("NS_OCR_PROBE"));
    if (path.isEmpty() || !QFile::exists(path)) QSKIP("NS_OCR_PROBE not set");
    QString reason;
    if (!RapidOcrEngine::available(&reason)) QSKIP(qPrintable(reason));
    RapidOcrEngine eng;
    QString err;
    auto res = eng.recognize({path}, &err);
    QVERIFY2(!res.isEmpty(), qPrintable(err));
    qInfo() << "pages:" << res.size() << "blocks:" << res.first().size();
    for (const auto &b : res.first())
      qInfo() << "block:" << b.content << b.rect;
    QVERIFY2(!res.first().isEmpty(), "no blocks on real art");
  }

  void smoke() {    QString reason;
    if (!RapidOcrEngine::available(&reason)) QSKIP(qPrintable(reason));
    // manga-like: big black lettering on white
    QImage img(900, 320, QImage::Format_RGB888);
    img.fill(Qt::white);
    QPainter p(&img);
    p.setPen(Qt::black);
    QFont f("Arial", 72, QFont::Bold);
    p.setFont(f);
    p.drawText(QRect(40, 20, 820, 130), Qt::AlignLeft, "HELLO WORLD");
    p.drawText(QRect(40, 170, 820, 130), Qt::AlignLeft, "MANGA TEST");
    p.end();
    QDir().mkpath("temp");
    const QString path = QDir::current().absoluteFilePath("temp/ocr_smoke.png");
    QVERIFY(img.save(path, "PNG"));

    RapidOcrEngine eng;
    QString err;
    auto res = eng.recognize({path}, &err);
    QVERIFY2(!res.isEmpty(), qPrintable(err));
    QString all;
    for (const auto &b : res.first()) {
      qInfo() << "block:" << b.content << b.rect;
      all += b.content + " ";
    }
    QVERIFY2(all.toUpper().contains("HELLO"), qPrintable(all));
    QVERIFY2(all.toUpper().contains("MANGA"), qPrintable(all));
  }
};

QTEST_MAIN(TOcr)
#include "t_ocr.moc"
