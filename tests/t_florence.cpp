#include <QtTest>
#include "services/florence_tok.h"
#include "services/florence_engine.h"

#include <QDir>

class TFlorence : public QObject {
  Q_OBJECT
private slots:
  QString tokPath() {
    const QStringList cands = {
      QDir::current().absoluteFilePath("models/florence/tokenizer.json"),
      QDir::current().absoluteFilePath("../models/florence/tokenizer.json"),
    };
    for (const auto &c : cands) if (QFile::exists(c)) return c;
    return {};
  }

  void bpeRoundTrip() {
    const QString tp = tokPath();
    if (tp.isEmpty()) QSKIP("florence tokenizer not downloaded");
    FlorenceBpe bpe;
    QString err;
    QVERIFY2(bpe.load(tp, &err), qPrintable(err));
    QVERIFY(bpe.vocabSize() > 51000);
    // task prompt encodes to ids...
    QVector<int> ids = bpe.encode("<MORE_DETAILED_CAPTION>");
    QVERIFY2(!ids.isEmpty(), "task prompt produced no tokens");
    qInfo() << "task ids:" << ids;
    // ...and plain words round-trip through byte BPE
    QVector<int> w = bpe.encode("Hello world");
    QVERIFY(!w.isEmpty());
    QCOMPARE(bpe.decode(w), QString("Hello world"));
  }

  void captionSmoke() {
    QString reason;
    if (!FlorenceCaptionEngine::available(&reason)) QSKIP(qPrintable(reason));
    const QString img = QDir::current().absoluteFilePath("temp/ocr_smoke.png");
    if (!QFile::exists(img)) QSKIP("smoke image missing (run ocr test first)");
    FlorenceCaptionEngine eng;
    QString err;
    const QString caption = eng.describe(img, &err);
    qInfo() << "caption:" << caption;
    QVERIFY2(!caption.isEmpty(), qPrintable(err));
    QVERIFY2(caption.size() > 20, qPrintable(caption));
  }
};

QTEST_MAIN(TFlorence)
#include "t_florence.moc"
