#include <QtTest>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <stdexcept>
#include "services/ai_service.h"
#include "services/narrate.h"
#include "services/tts_service.h"

class TNarration : public QObject {
  Q_OBJECT
private slots:
  void promptHasRules() {
    NS::Project p; p.name = "Test"; p.synopsis = "world";
    NS::Character c; c.name = "MC"; c.role = "hero"; p.characters << c;
    NS::Page pg; pg.number = 1; pg.description = "forest"; pg.cast << "MC";
    NS::TextBlock b; b.content = "hello"; b.speaker = "MC"; pg.blocks << b;
    p.pages << pg;
    QString prompt = NS::Narrator::buildPrompt(p, 1, 1);
    QVERIFY(prompt.contains("ONE calm narrator"));
    QVERIFY(prompt.contains("SYNOPSIS"));
    QVERIFY(prompt.contains("[PAGE n]"));
    QVERIFY(prompt.contains("MC"));
  }
  void parseTags() {
    QString raw = "First page prose.\n[PAGE 2]\nSecond page prose.";
    auto [clean, segs] = NS::Narrator::parseTagged(raw, 1, 2);
    QVERIFY(!clean.contains("[PAGE"));
    QCOMPARE(segs.size(), 2);
    QCOMPARE(segs[0].page, 1);
    QCOMPARE(segs[1].page, 2);
  }
  void chunk() {
    NS::Project p;
    for (int i = 1; i <= 5; ++i) { NS::Page pg; pg.number = i; pg.description = "x"; p.pages << pg; }
    int end = NS::Narrator::largestFittingChunkEnd(p, 1, 200);
    QVERIFY(end >= 1 && end <= 5);
  }
  void tactics() {
    // error classification mirrors Python's worker regexes
    QCOMPARE(NS::failoverReason("The model is overloaded. Please try again later"), QString("overloaded"));
    QCOMPARE(NS::failoverReason("503 Service Unavailable"), QString("overloaded"));
    QCOMPARE(NS::failoverReason("Model not found: gemini-1.5-flash"), QString("unavailable"));
    QCOMPARE(NS::failoverReason("Quota exceeded for quota metric"), QString(""));
    QVERIFY(NS::isRateLimitError("429 RESOURCE_EXHAUSTED, retry in 22.07s"));
    QVERIFY(NS::isTooLargeError("request too large: input too long"));
    QCOMPARE(NS::retryAfterSeconds("Please retry in 22.077145941s"), 22.077145941);
    QCOMPARE(NS::retryAfterSeconds("boom"), -1.0);
    QVERIFY(NS::estimatePromptTokens("abcd") >= 1);
  }
  void chunkedJoin() {
    // fake backend: one tagged chunk per call, pages advance 1-2 then 3
    NS::Project p;
    for (int i = 1; i <= 3; ++i) {
      NS::Page pg; pg.number = i;
      pg.description = QString(800, QChar('x')); // force small chunks
      p.pages << pg;
    }
    NS::NarrateBackend be;
    be.label = "fake"; be.maxPromptTokens = 512; // floor: ~2048 chars/chunk
    be.models = {"m1"};
    int calls = 0;
    be.request = [&](const QString &, const QString &, const QString &prompt) {
      ++calls;
      Q_UNUSED(prompt);
      static int n = 0; ++n;
      if (n == 1) return QString("[PAGE 1]\nFirst.\n[PAGE 2]\nSecond.");
      return QString("[PAGE 3]\nThird.");
    };
    NS::NarrateLimits lim;
    lim.chunkPages = 10;
    auto res = NS::narrateChapter(p, 1, 3, {}, be, lim,
                                  [](int, const QString &){}, []{ return false; });
    QVERIFY(!res.failed);
    QVERIFY(calls >= 2); // chunked, not one request
    QVERIFY(res.script.contains("First."));
    QVERIFY(res.script.contains("Third."));
    QCOMPARE(res.lastDone, 3);
    // every page covered, offsets monotonic, spans the whole script
    QSet<int> pages;
    int prevEnd = 0;
    for (auto &s : res.segments) {
      pages.insert(s.page);
      QVERIFY(s.start >= prevEnd);
      QVERIFY(s.end >= s.start);
      prevEnd = s.end;
    }
    for (int n = 1; n <= 3; ++n) QVERIFY(pages.contains(n));
    QCOMPARE(prevEnd, res.script.size());
  }
    void failoverSwitch() {    NS::Project p;
    NS::Page pg; pg.number = 1; pg.description = "x"; p.pages << pg;
    NS::NarrateBackend be;
    be.maxPromptTokens = 4000;
    be.models = {"bad", "good"};
    be.request = [](const QString &model, const QString &, const QString &) -> QString {
      if (model == "bad") throw std::runtime_error("The model is overloaded");
      return QString("[PAGE 1]\nDone.");
    };
    auto res = NS::narrateChapter(p, 1, 1, {}, be, NS::NarrateLimits(),
                                  [](int, const QString &){}, []{ return false; });
    QVERIFY(!res.failed);
    QVERIFY(res.script.contains("Done."));
  }
  void pageSlices() {    const QString script = "AAAABBBBCCCC"; // 12 chars, 3 pages
    QVector<NS::PageSegment> segs = {{1, 0, 4}, {2, 4, 8}, {3, 8, 12}};
    auto out = NS::pageSlices(script, segs, 1, 3);
    QCOMPARE(out.size(), 3);
    QCOMPARE(out[0], QString("AAAA"));
    QCOMPARE(out[1], QString("BBBB"));
    QCOMPARE(out[2], QString("CCCC"));
    // stale segments (user edited the script) -> even split
    auto even = NS::pageSlices(script + "!", segs, 1, 3);
    QCOMPARE(even.size(), 3);
    QCOMPARE((even[0] + even[1] + even[2]).size(), 13);
    // no segments -> even split
    auto none = NS::pageSlices(script, {}, 1, 3);
    QCOMPARE(none.size(), 3);
    QCOMPARE(none[0], QString("AAAA"));
  }
  void appendContinuation() {    // Part 1 (pages 1-2) + part 2 (page 3): absolute offsets, valid cover.
    QVector<NS::PageSegment> s1 = {{1, 0, 4}, {2, 4, 8}};
    QVector<NS::PageSegment> s2 = {{3, 0, 4}};
    auto [full, segs] = NS::appendScript("AAAABBBB", s1, "CCCC", s2);
    QCOMPARE(full, QString("AAAABBBB\n\nCCCC"));
    QCOMPARE(segs.size(), 3);
    QCOMPARE(segs[2].page, 3);
    QCOMPARE(segs[2].start, 10);
    QCOMPARE(segs[2].end, 14);
    QVERIFY(NS::narrationSegmentsValid(full, segs, 1, 3));
    // slicing the joined script hits every page exactly
    auto out = NS::pageSlices(full, segs, 1, 3);
    QCOMPARE(out.size(), 3);
    QCOMPARE(out[2], QString("CCCC"));
    // empty old script: chunk stands alone
    auto [f2, s0] = NS::appendScript("", {}, "Hi", QVector<NS::PageSegment>{{1, 0, 2}});
    QCOMPARE(f2, QString("Hi"));
    QCOMPARE(s0.size(), 1);
  }
  void quotaRotation() {
    QVERIFY(NS::isQuotaExhaustedError("Quota exhausted, retry in 5000s"));
    QVERIFY(NS::isQuotaExhaustedError("Daily quota spent, resets at midnight Pacific"));
    QVERIFY(!NS::isQuotaExhaustedError("Please retry in 22.07s"));
    QVERIFY(!NS::isQuotaExhaustedError("429 too many requests"));
    // key1 spent (long retry) -> same chunk retried on key2 -> success
    NS::Project p;
    NS::Page pg; pg.number = 1; pg.description = "x"; p.pages << pg;
    NS::NarrateBackend be;
    be.maxPromptTokens = 4000;
    be.models = {"m"};
    be.apiKeys = {"k1", "k2"};
    QStringList seen;
    be.request = [&](const QString &, const QString &key, const QString &) -> QString {
      seen << key;
      if (key == "k1") throw std::runtime_error("Quota exhausted, retry in 5000s");
      return QString("[PAGE 1]\nDone.");
    };
    auto res = NS::narrateChapter(p, 1, 1, {}, be, NS::NarrateLimits(),
                                  [](int, const QString &){}, []{ return false; });
    QVERIFY(!res.failed);
    QVERIFY(res.script.contains("Done."));
    QCOMPARE(seen, QStringList({"k1", "k2"}));
    // every key spent -> clean failure naming the count
    NS::NarrateBackend be2;
    be2.maxPromptTokens = 4000;
    be2.models = {"m"};
    be2.apiKeys = {"k1", "k2"};
    be2.request = [](const QString &, const QString &, const QString &) -> QString {
      throw std::runtime_error("Daily quota spent, resets at midnight Pacific");
    };
    auto res2 = NS::narrateChapter(p, 1, 1, {}, be2, NS::NarrateLimits(),
                                   [](int, const QString &){}, []{ return false; });
    QVERIFY(res2.failed);
    QVERIFY(res2.error.contains("2 key"));
  }
  void netProbe() {
    // Same call the app makes, bogus key: must fail with a structured,
    // scrubbed message (never the key, never a bare "server replied").
    OpenAiCompatNarrator nar(
      "https://generativelanguage.googleapis.com/v1beta/openai",
      "INVALID-KEY-PROBE-1234567890", "gemini-3.6-flash");
    QString err;
    QString out = nar.narrate("Reply with exactly: OK", &err);
    qInfo() << "probe err:" << err;
    QVERIFY(out.trimmed().isEmpty());
    QVERIFY(!err.isEmpty());
    QVERIFY2(!err.contains("INVALID-KEY-PROBE"), qPrintable(err));
    QVERIFY2(!err.contains("key="), qPrintable(err));
  }
  void netDiag() {    // Raw Qt request diagnostics: status, headers, body size.
    QNetworkAccessManager nam;
    QNetworkRequest req(QUrl("https://generativelanguage.googleapis.com/v1beta/openai/chat/completions"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("x-goog-api-key", "INVALID-KEY-PROBE-1234567890");
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);
    QNetworkReply *rep = nam.post(req, QByteArray("{\"model\":\"gemini-3.6-flash\",\"messages\":[{\"role\":\"user\",\"content\":\"hi\"}]}"));
    QEventLoop loop;
    QObject::connect(rep, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    qInfo() << "error:" << rep->error() << rep->errorString();
    qInfo() << "httpStatus:" << rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    qInfo() << "headers:" << rep->rawHeaderPairs().size();
    for (auto &h : rep->rawHeaderPairs()) qInfo() << "  " << h.first << "=" << h.second.left(80);
    const QByteArray body = rep->readAll();
    qInfo() << "bodyBytes:" << body.size() << QString::fromUtf8(body).left(300);
    qInfo() << "http2Used:" << rep->attribute(QNetworkRequest::Http2WasUsedAttribute).toBool();
    rep->deleteLater();
  }
  void netBisect() {    // postJsonSync replica with knobs: timer on/off, http2 on/off.
    for (int variant = 0; variant < 4; ++variant) {
      const bool useTimer = (variant & 1) != 0;
      const bool useHttp2 = (variant & 2) != 0;
      QNetworkAccessManager nam;
      QNetworkRequest req(QUrl("https://generativelanguage.googleapis.com/v1beta/openai/chat/completions"));
      req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
      req.setRawHeader("x-goog-api-key", "INVALID-KEY-PROBE-1234567890");
      if (useHttp2) req.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);
      QNetworkReply *rep = nam.post(req, QByteArray("{\"model\":\"gemini-3.6-flash\",\"messages\":[{\"role\":\"user\",\"content\":\"hi\"}],\"temperature\":0.7}"));
      QEventLoop loop;
      QTimer t; t.setSingleShot(true);
      if (useTimer) {
        QObject::connect(&t, &QTimer::timeout, &loop, [&]{ if (rep->isRunning()) rep->abort(); });
        t.start(300000);
      }
      QObject::connect(rep, &QNetworkReply::finished, &loop, &QEventLoop::quit);
      loop.exec();
      const QByteArray body = rep->readAll();
      qInfo() << "variant" << variant << "timer:" << useTimer << "http2attr:" << useHttp2
              << "err:" << rep->error()
              << "status:" << rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
              << "h2:" << rep->attribute(QNetworkRequest::Http2WasUsedAttribute).toBool()
              << "bytes:" << body.size();
      rep->deleteLater();
    }
  }
  void netReplica() {    // byte-for-byte replica of postJsonSync's error path
    QNetworkAccessManager nam;
    QNetworkRequest req(QUrl("https://generativelanguage.googleapis.com/v1beta/openai/chat/completions"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QList<QPair<QByteArray,QByteArray>> headers;
    headers.append({"x-goog-api-key", QByteArray("INVALID-KEY-PROBE-1234567890")});
    for (auto &h : headers) req.setRawHeader(h.first, h.second);
    QJsonObject msg; msg["role"] = "user"; msg["content"] = "Reply with exactly: OK";
    QJsonObject b; b["model"] = "gemini-3.6-flash";
    b["messages"] = QJsonArray{msg}; b["temperature"] = 0.7;
    const QByteArray body = QJsonDocument(b).toJson();
    QNetworkReply *rep = nam.post(req, body);
    QEventLoop loop; QTimer t; t.setSingleShot(true);
    QObject::connect(&t, &QTimer::timeout, &loop, [&]{ if (rep->isRunning()) rep->abort(); });
    QObject::connect(rep, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    t.start(300000); loop.exec();
    qInfo() << "replica err:" << rep->error();
    const QByteArray raw = rep->readAll();
    qInfo() << "replica bytes:" << raw.size() << QString::fromUtf8(raw).left(200);
    rep->deleteLater();
  }
  void netDirect() {
    // Call the real postJsonSync with narrate-identical args.
    QJsonObject msg; msg["role"] = "user"; msg["content"] = "Reply with exactly: OK";
    QJsonObject b; b["model"] = "gemini-3.6-flash";
    b["messages"] = QJsonArray{msg}; b["temperature"] = 0.7;
    QList<QPair<QByteArray,QByteArray>> headers;
    headers.append({"x-goog-api-key", QByteArray("INVALID-KEY-PROBE-1234567890")});
    QByteArray reply;
    QString err;
    const QByteArray namedBody = QJsonDocument(b).toJson(); // named, not temporary
    postJsonSync(QUrl("https://generativelanguage.googleapis.com/v1beta/openai/chat/completions"),
                 namedBody, headers, &reply, &err, 300000);
    qInfo() << "direct reply bytes:" << reply.size() << "err:" << err;
  }
};

QTEST_MAIN(TNarration)
#include "t_narration.moc"
