#include <QtTest>
#include "core/project.h"
#include "database/projects.h"
#include <QDir>
#include <QSqlDatabase>

class TDatabase : public QObject {
  Q_OBJECT
private slots:
  void roundTrip() {
    NS::Project p;
    p.name = "Test Tale";
    p.synopsis = "World premise here.";
    p.narration = "Once upon a time.";
    p.audioPath = "C:/fake/narration.wav";
    p.markers = {1.5, 3.25, 7.0};
    p.workStart = 2; p.workEnd = 5;
    NS::Character c1; c1.name = "MC"; c1.role = "hero"; c1.voiceStyle = "calm";
    NS::Character c2; c2.name = "Yuki"; c2.role = "sidekick";
    p.characters << c1 << c2;
    for (int n = 1; n <= 3; ++n) {
      NS::Page pg; pg.number = n;
      pg.imagePath = QString("C:/fake/page_%1.png").arg(n);
      pg.description = QString("Desc %1").arg(n);
      NS::TextBlock b; b.content = QString("Hello %1").arg(n);
      b.type = "dialogue"; b.speaker = (n == 1 ? "MC" : "");
      pg.blocks << b;
      if (n < 3) pg.cast << "MC";
      if (n > 1) pg.cast << "Yuki";
      p.pages << pg;
    }
    QDir().mkpath("temp");
    const QString db = QDir::current().absoluteFilePath("temp/t_roundtrip.db");
    QFile::remove(db);
    QString err;
    QVERIFY2(NS::ProjectStore::save(db, p, &err), qPrintable(err));
    // save twice to the same path (autosave pattern: rewrite over old file)
    QFile::remove(db);
    QVERIFY2(NS::ProjectStore::save(db, p, &err), qPrintable(err));

    NS::Project q;
    QVERIFY2(NS::ProjectStore::load(db, q, &err), qPrintable(err));
    QCOMPARE(q.name, p.name);
    QCOMPARE(q.synopsis, p.synopsis);
    QCOMPARE(q.narration, p.narration);
    QCOMPARE(q.audioPath, QString("C:/fake/narration.wav"));
    QCOMPARE(q.markers, QVector<double>({1.5, 3.25, 7.0}));
    QCOMPARE(q.workStart, 2);
    QCOMPARE(q.workEnd, 5);
    QCOMPARE(q.characters.size(), 2);
    QCOMPARE(q.pages.size(), 3);
    QCOMPARE(q.pages[0].blocks.size(), 1);
    QCOMPARE(q.pages[0].blocks.first().content, QString("Hello 1"));
    QCOMPARE(q.pages[0].cast, QStringList({"MC"}));
    QCOMPARE(q.pages[1].cast.size(), 2);
    QCOMPARE(q.pages[2].description, QString("Desc 3"));
    // connection hygiene: no leaked handles per open
    qInfo() << "connections:" << QSqlDatabase::connectionNames().size();
    QCOMPARE(QSqlDatabase::connectionNames().size(), 0);
  }
  void nullTexts() {
    // Fresh-import shape: null descriptions/synopsis must save, not trip
    // NOT NULL columns (regression: "insert page NOT NULL ... description").
    NS::Project p;
    p.name = "Fresh";
    NS::Page pg; pg.number = 1; pg.imagePath = "C:/fake/a.png";
    // description/synopsis/narration left null on purpose
    p.pages << pg;
    const QString db = QDir::current().absoluteFilePath("temp/t_nulls.db");
    QFile::remove(db);
    QString err;
    QVERIFY2(NS::ProjectStore::save(db, p, &err), qPrintable(err));
    NS::Project q;
    QVERIFY2(NS::ProjectStore::load(db, q, &err), qPrintable(err));
    QCOMPARE(q.pages.size(), 1);
    QCOMPARE(q.pages.first().description, QString(""));
  }
};

QTEST_MAIN(TDatabase)
#include "t_database.moc"
