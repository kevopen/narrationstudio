#include <QtTest>
#include "core/timeline.h"

class TTimeline : public QObject {
  Q_OBJECT
private slots:
  void basic() {
    // 3 pages, 30s, marker at 10 -> [10, 10, 10]
    auto d = NS::Timeline::markersToDurations({10.0}, 30.0, 3);
    QCOMPARE(d.size(), 3);
    QVERIFY(qAbs(d[0] - 10.0) < 1e-6);
    QVERIFY(qAbs(d[1] - 10.0) < 1e-6);
    QVERIFY(qAbs(d[2] - 10.0) < 1e-6);
  }
  void dedupe() {
    // near-identical markers collapse (no zero-duration page)
    auto d = NS::Timeline::markersToDurations({10.0, 10.02}, 30.0, 3);
    QCOMPARE(d.size(), 3);
    QVERIFY(d[0] > 1.0 && d[1] > 1.0);
  }
  void roundtrip() {
    QVector<double> durs = {4.0, 5.0, 6.0};
    auto m = NS::Timeline::durationsToMarkers(durs, 15.0);
    QCOMPARE(m.size(), 2);
    auto back = NS::Timeline::markersToDurations(m, 15.0, 3);
    QVERIFY(qAbs(back[0] - 4.0) < 1e-6);
  }
  void strip() {
    QString t = "Hello\n[PAGE 2]\nWorld\nPAGE 3:\npage 4 mentioned mid-sentence stays";
    QString s = NS::Timeline::stripPageMarkers(t);
    QVERIFY(!s.contains("[PAGE 2]"));
    QVERIFY(s.contains("page 4 mentioned"));
  }
};

QTEST_MAIN(TTimeline)
#include "t_timeline.moc"
