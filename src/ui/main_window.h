#pragma once
#include "core/project.h"
#include "core/settings.h"
#include <QMainWindow>

class QListWidget;
class QCheckBox;
class QDockWidget;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;
class QPlainTextEdit;
class QMediaPlayer;
class QMenu;
class QProgressBar;
class QScrollArea;
class QToolBar;
class TimelineWidget;

// Glass editor: workflow stepper on top, thumbnail gallery left,
// glass stage center, inspector right, timeline bottom.
// Dock geometry persists in QSettings.
class MainWindow : public QMainWindow {
  Q_OBJECT
  friend class UiCrashProbe; // offscreen UI regression tests may drive privates
public:
  explicit MainWindow(QWidget *parent = nullptr);

private slots:
  void importFolder();
  void importPdf();
  void importMangaDx();
  void importWebtoon();
  void appendFolder();
  void appendPdf();
  void appendMangaDx();
  void appendWebtoon();
  void runOcr();
  void runDescribe();
  void runNarrate();
  void runSynthesize();
  void runExport();
  void saveProject();
  void loadProject();
  void editCharacters();
  void editSynopsis();
  void editDescription();
  void editVoice();
  void editSettings();
  void editWorkRange();
  void editDurations();
  void onPageSelected(int row);
  void zoomIn();
  void zoomOut();
  void autosave();

private:
  void buildActions();
  void buildStepBar();
  void buildDocks();
  void refreshPageList();
  void loadThumbnails();
  void updatePagesTitle();
  void setProjectPages(const QString &name, const QStringList &images);
  // Continuation: number new pages after the last one, keep everything,
  // point the work range at the new span only.
  void appendProjectPages(const QStringList &images);
  void showPage(int idx);
  void fitViewer();
  bool eventFilter(QObject *obj, QEvent *ev) override;
  void closeEvent(QCloseEvent *ev) override;
  void setStep(const QString &s);

  void setStatus(const QString &s, int pct = -1);
  void setBusy(bool busy);   // locks workflow steps while one runs

  NS::Project m_project;
  QString m_dbPath = "projects/autosave.db";
  AppSettings m_settings;

  QToolBar *m_stepBar = nullptr;
  QLabel *m_stepStatus = nullptr;
  QMenu *m_toolsMenu = nullptr;
  QProgressBar *m_progress = nullptr;
  QDockWidget *m_pagesDock = nullptr;
  QPushButton *m_playBtn = nullptr;
  QLabel *m_posLabel = nullptr;
  QCheckBox *m_followCheck = nullptr;
  int m_busyCount = 0;
  QListWidget *m_pageList = nullptr;
  QScrollArea *m_stageScroll = nullptr;
  QPlainTextEdit *m_descEdit = nullptr;
  QPixmap m_pagePixmap;
  QLabel *m_viewer = nullptr, *m_castLabel = nullptr, *m_zoomLabel = nullptr;
  QWidget *m_castRow = nullptr;
  QTabWidget *m_tabs = nullptr;
  QPlainTextEdit *m_blocksEdit = nullptr, *m_narrationEdit = nullptr, *m_reviewEdit = nullptr;
  TimelineWidget *m_timeline = nullptr;
  QMediaPlayer *m_preview = nullptr;
  int m_current = 0;
  double m_zoom = 1.0;
};
