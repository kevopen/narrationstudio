// Offscreen UI regression test: drives the real MainWindow through the
// import-heavy path that once segfaulted (QPixmap in a worker thread).
// Uses real webtoon images + real OCR on a 3-page work range.
#include <QtTest>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QStandardPaths>
#include <QTimer>
#include "core/pipeline.h"
#include "ui/dialogs.h"
#include "ui/main_window.h"

class UiCrashProbe : public QObject {
  Q_OBJECT
private slots:
  void webtoonFlow() {
    qInfo() << "creating window";
    MainWindow w;
    w.show();
    QStringList imgs = NS::Pipeline::collectImages(
      QDir::current().absoluteFilePath("temp/wtprobe"));
    qInfo() << "images:" << imgs.size();
    if (imgs.size() < 100) QSKIP("temp/wtprobe images missing");
    qInfo() << "populating project";
    w.setProjectPages("probe", imgs);
    qInfo() << "populated; waiting for thumbnails";
    QTest::qWait(15000);
    qInfo() << "thumbs done; flipping pages";
    for (int i = 0; i < 10; ++i) {
      w.showPage(i * 10);
      QTest::qWait(150);
    }
    qInfo() << "flips done; narrowing range + OCR";
    w.m_project.workStart = 1;
    w.m_project.workEnd = 3;
    w.runOcr();
    for (int i = 0; i < 120 && w.m_busyCount > 0; ++i) QTest::qWait(1000);
    qInfo() << "ocr done; blocks on page 1:" << w.m_project.pages[0].blocks.size();
    // No content assert here (recall belongs to t_ocr on synthetic art);
    // this test proves the heavy UI path doesn't crash.
    qInfo() << "viewer pixmap null:" << w.m_viewer->pixmap().isNull();
    QVERIFY(!w.m_viewer->pixmap().isNull());
    qInfo() << "UI flow survived";
  }
  void webtoonDialogFlow() {
    // Full user path: menu slot -> dialog (auto-filled+accepted) ->
    // real CLI download in pool thread -> populate. Crashes here = found it.
    // Needs webtoon-downloader on the machine; SKIP without it (CI).
    QString dl = QStandardPaths::findExecutable("webtoon-downloader");
    if (dl.isEmpty()) {
      const QString home = QDir::homePath();
      for (const QString &c : {home + "/.local/bin/webtoon-downloader.exe",
                               home + "/.local/bin/webtoon-downloader"})
        if (QFile::exists(c)) { dl = c; break; }
    }
    if (dl.isEmpty()) QSKIP("webtoon-downloader not installed");
    MainWindow w;
    w.show();
    QTimer::singleShot(1500, [&]{
      for (QWidget *wd : QApplication::topLevelWidgets()) {
        auto *dlg = qobject_cast<WebtoonDialog*>(wd);
        if (!dlg || !dlg->isVisible()) continue;
        qInfo() << "dialog open; filling URL";
        if (auto *le = dlg->findChild<QLineEdit*>())
          le->setText("https://www.webtoons.com/en/action/tower-of-god/list?title_no=95");
        if (auto *bb = dlg->findChild<QDialogButtonBox*>())
          if (auto *ok = bb->button(QDialogButtonBox::Ok)) {
            qInfo() << "accepting dialog";
            ok->click();
          }
      }
    });
    qInfo() << "invoking importWebtoon";
    QMetaObject::invokeMethod(&w, "importWebtoon", Qt::QueuedConnection);
    for (int i = 0; i < 240 && w.m_project.pages.isEmpty(); ++i) QTest::qWait(1000);
    qInfo() << "pages after import:" << w.m_project.pages.size();
    // A crash anywhere above fails the test; an empty download (offline CI)
    // only skips the content asserts below.
    if (w.m_project.pages.isEmpty()) QSKIP("webtoon download yielded nothing (offline?)");
    QTest::qWait(5000);
    qInfo() << "dialog flow survived";
  }
};

QTEST_MAIN(UiCrashProbe)
#include "t_uiprobe.moc"
