#include <QtTest>
#include <cmath>
#include "services/kokoro_engine.h"
#include "services/kokoro_g2p.h"
#include "services/kokoro_npz.h"
#include "services/misaki_lite.h"
#include "core/timeline.h"

#include <QDir>
#include <QJsonDocument>

class TKokoro : public QObject {
  Q_OBJECT
private slots:
  void vocabLoads() {
    // vocab resource ships with the app (fallback: resources/ dir)
    QFile f(":/kokoro_vocab.json");
    if (!f.open(QIODevice::ReadOnly)) {
      QString dev = QDir::current().absoluteFilePath("resources/kokoro_vocab.json");
      if (!QFile::exists(dev))
        dev = QDir::current().absoluteFilePath("../resources/kokoro_vocab.json");
      f.setFileName(dev);
      QVERIFY2(f.open(QIODevice::ReadOnly), "kokoro vocab missing");
    }
    const QJsonObject vocab = QJsonDocument::fromJson(f.readAll()).object()["vocab"].toObject();
    QVERIFY(vocab.size() > 100);
    QCOMPARE(vocab[" "].toInt(), 16);
    QVERIFY(vocab.contains(QString::fromUtf8("ə")));
    QVERIFY(vocab.contains(QString::fromUtf8("ˈ")));
  }

  void vocabFilter() {
    QMap<QString,int> v;
    v["h"] = 50; v["a"] = 43; v[" "] = 16; v[QString::fromUtf8("ə")] = 83;
    // unknown chars (digits, ties) drop; spaces collapse
    QCOMPARE(EspeakG2P::filterVocab("  h1a  ə  ", v), QString("ha ə"));
  }

  void npzVoices() {
    // needs models/kokoro/voices-v1.0.bin (fetch-kokoro.ps1); skip without it
    const QStringList cands = {
      QDir::current().absoluteFilePath("models/kokoro/voices-v1.0.bin"),
      QDir::current().absoluteFilePath("../models/kokoro/voices-v1.0.bin"),
    };
    QString path;
    for (const auto &c : cands) if (QFile::exists(c)) { path = c; break; }
    if (path.isEmpty()) QSKIP("voices file not downloaded");
    NpzVoices npz;
    QString err;
    QVERIFY2(npz.load(path, &err), qPrintable(err));
    QVERIFY(npz.names().contains("af_heart"));
    NpzVoices::VoiceTensor t;
    QVERIFY2(npz.extract("af_heart", &t, &err), qPrintable(err));
    QCOMPARE(t.cols, 256);
    QVERIFY(t.rows >= 500);
  }

  void sentenceRanges() {    // engine maps one boundary per sentence via the shared splitter
    const QString text = "Hello there. How are you? Fine!";
    auto ranges = NS::Timeline::splitSentences(text);
    QCOMPARE(ranges.size(), 3);
    QCOMPARE(text.mid(ranges[0].first, ranges[0].second - ranges[0].first),
             QString("Hello there."));
  }

  void synthesizeSmoke() {
    // Full chain: espeak → vocab → ORT int8 → wav. Needs runtime files.
    QString reason;
    if (!KokoroTtsEngine::available(&reason)) QSKIP(qPrintable(reason));
    KokoroTtsEngine eng("af_heart", 1.0);
    QDir().mkpath("temp");
    const QString wav = QDir::current().absoluteFilePath("temp/kokoro_smoke.wav");
    QVector<ITtsEngine::Boundary> bounds;
    QString err;
    QVERIFY2(eng.synthesize("Hello! This is Kokoro speaking.", wav, &bounds, &err),
             qPrintable(err));
    QFile f(wav);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QVERIFY2(f.size() > 48044, qPrintable(QString::number(f.size())));
    // voice must be audible, not silence: RMS check on int16 body
    f.seek(44);
    const QByteArray body = f.readAll();
    const qint16 *s = reinterpret_cast<const qint16 *>(body.constData());
    const qsizetype n = body.size() / 2;
    double sum = 0;
    for (qsizetype i = 0; i < n; ++i) sum += double(s[i]) * s[i];
    const double rms = std::sqrt(sum / qMax<qsizetype>(1, n));
    qInfo() << "smoke wav seconds:" << (n / 24000.0) << "rms:" << rms
            << "bounds:" << bounds.size();
    QVERIFY2(rms > 300, "synthesized audio is silent");
    QVERIFY(bounds.size() >= 3); // 2 sentences + final

    // Multi-page path (what Voice step uses): one session, per-page wavs.
    QStringList wavs;
    QVector<QVector<ITtsEngine::Boundary>> perPage;
    int progressCalls = 0;
    QVERIFY2(eng.synthesizePages({"Hi there.", "Second page here."},
                                 QDir::current().absoluteFilePath("temp/kokoro_pages"),
                                 &wavs, &perPage,
                                 [&](int, int){ ++progressCalls; }, &err),
             qPrintable(err));
    QCOMPARE(wavs.size(), 2);
    QCOMPARE(perPage.size(), 2);
    QVERIFY(progressCalls >= 2);
    for (const QString &wp : wavs) {
      QFile g(wp);
      QVERIFY(g.open(QIODevice::ReadOnly));
      const QByteArray blob = g.readAll();
      QVERIFY2(blob.size() > 10044, qPrintable(wp + " " + QString::number(blob.size())));
      const qint16 *s2 = reinterpret_cast<const qint16 *>(blob.constData() + 44);
      const qsizetype n2 = (blob.size() - 44) / 2;
      double sum2 = 0;
      for (qsizetype i = 0; i < n2; ++i) sum2 += double(s2[i]) * s2[i];
      QVERIFY2(std::sqrt(sum2 / qMax<qsizetype>(1, n2)) > 300, qPrintable(wp));
    }
  }

  void g2pSmokes() {
    // needs third_party/espeak-ng (fetch-kokoro.ps1); skip without it
    const QStringList libs = {
      QDir::current().absoluteFilePath("third_party/espeak-ng/espeak-ng.dll"),
      QDir::current().absoluteFilePath("../third_party/espeak-ng/espeak-ng.dll"),
    };
    const QStringList datas = {
      QDir::current().absoluteFilePath("third_party/espeak-ng/espeak-ng-data"),
      QDir::current().absoluteFilePath("../third_party/espeak-ng/espeak-ng-data"),
    };
    QString lib, data;
    for (const auto &c : libs) if (QFile::exists(c)) { lib = c; break; }
    for (const auto &c : datas) if (QDir(c).exists()) { data = c; break; }
    if (lib.isEmpty() || data.isEmpty()) QSKIP("espeak-ng not downloaded");
    EspeakG2P g2p(lib, data);
    QVERIFY2(g2p.isReady(), qPrintable(g2p.error()));
    const QString ph = g2p.phonemize("Hello, world!");
    qInfo() << "phoneme length:" << ph.size();
    QVERIFY2(!ph.isEmpty(), "empty phonemes");
    // Whole sentence must survive (clause loop), with IPA stress marks
    QVERIFY2(ph.size() > 10, qPrintable(ph));
    QVERIFY2(ph.contains(QString::fromUtf8("ˈ")), qPrintable(ph));
    QVERIFY2(ph.contains(","), qPrintable(ph));
  }
  void misakiLite() {
    // Lexicon-first G2P (misaki port): curated stress, no espeak needed here.
    MisakiLite m;
    QString err;
    const QStringList dirs = {
      QDir::current().absoluteFilePath("resources/misaki"),
      QDir::current().absoluteFilePath("../resources/misaki"),
    };
    QString dir;
    for (const auto &c : dirs) if (QFile::exists(c + "/us_gold.json")) { dir = c; break; }
    if (dir.isEmpty()) QSKIP("misaki lexicons missing");
    QVERIFY2(m.load(dir, &err), qPrintable(err));
    QCOMPARE(m.lookupWord("sudden"), QString("s\u02c8\u028cd\u1d4an"));
    QCOMPARE(m.lookupWord("nation"), QString("n\u02c8A\u0283\u0259n"));
    QCOMPARE(m.lookupWord("a"), QString("\u0259"));
    QCOMPARE(m.lookupWord("I"), QString("\u02ccI"));
    QCOMPARE(m.lookupWord("the", "apple"), QString("\u00f0i"));
    QCOMPARE(m.lookupWord("the", "book"), QString("\u00f0\u0259"));
    QCOMPARE(m.lookupWord("to", "apple"), QString("tu"));
    QCOMPARE(m.lookupWord("to", "bed"), QString("t\u0259"));
    QCOMPARE(m.lookupWord("ends"), QString("\u02c8\u025bndz"));
    QCOMPARE(m.lookupWord("boxes"), QString("b\u02c8\u0251ks\u1d7bz"));
    // heteronyms: context picks the form (misaki POS without spacy)
    QCOMPARE(m.lookupWord("close", "the door", "to"), QString("kl\u02c8Oz"));
    QCOMPARE(m.lookupWord("close", {}, "the"), QString("kl\u02c8Oz"));
    QCOMPARE(m.lookupWord("close", {}, "sudden"), QString("kl\u02c8Oz"));
    QCOMPARE(m.lookupWord("read", "books", "he"), QString("\u0279\u02c8\u025bd"));
    // sentence keeps punctuation and natural (non-espeak-per-word) stress
    const QString ph = m.phonemize("That was the moment everything truly ended, as the fall of the nation brought life to a sudden close.");
    qInfo() << "SENT:" << ph;
    QVERIFY(ph.contains(QString("\u02c8")));
    const QString ph2 = m.phonemize("The human suddenly closed the nation.");
    qInfo() << "HUMAN-SENT:" << ph2;
    QVERIFY(ph.contains(",") || ph.contains("."));
    // applyStress units
    QCOMPARE(MisakiLite::applyStress("test", false, 0.0), QString("test"));
    QVERIFY(MisakiLite::applyStress("sudden", true, 2.0).contains(QString("\u02c8")));
  }
};

QTEST_MAIN(TKokoro)
#include "t_kokoro.moc"
