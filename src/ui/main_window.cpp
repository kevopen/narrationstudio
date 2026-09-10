#include "main_window.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include "core/pipeline.h"
#include "core/timeline.h"
#include "database/projects.h"
#include "dialogs.h"
#include "glass.h"
#include "icons.h"
#include "services/ai_service.h"
#include "services/florence_engine.h"
#include "services/kokoro_engine.h"
#include "services/narrate.h"
#include "services/mangadx_service.h"
#include "services/rapid_ocr.h"
#include "services/tts_service.h"
#include "services/webtoon_service.h"
#include "services/engines.h"
#include "services/video_service.h"
#include "timeline_widget.h"
#include "workers.h"

#include <QAudioOutput>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLayout>
#include <QListWidget>
#include <QMediaPlayer>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QThreadPool>
#include <QVBoxLayout>
#include <memory>
#include <QToolBar>
#include <QtConcurrent>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
  setWindowTitle(QString("NarrationStudio %1 (built %2 %3)")
                 .arg(QString::fromLatin1(NS_APP_VERSION),
                      QString::fromLatin1(__DATE__), QString::fromLatin1(__TIME__)));
  resize(1280, 840);
  Glass::apply(this);   // native rounded frame + dark titlebar + Mica (Win11)
  m_zoom = m_settings.pageZoom();
  m_preview = new QMediaPlayer(this);
  m_preview->setAudioOutput(new QAudioOutput(this));
  buildActions();
  buildDocks();
  restoreState(m_settings.mainWindowState(1));
  setStatus("Ready - File > Import folder or PDF to start.");
}

void MainWindow::buildActions() {
  auto *file = menuBar()->addMenu("&File");
  file->addAction("Import folder...", this, &MainWindow::importFolder);
  file->addAction("Import PDF...", this, &MainWindow::importPdf);
  file->addAction("Import from MangaDx...", this, &MainWindow::importMangaDx);
  file->addAction("Import from Webtoon...", this, &MainWindow::importWebtoon);
  file->addSeparator();
  file->addAction("Append folder...", this, &MainWindow::appendFolder);
  file->addAction("Append PDF...", this, &MainWindow::appendPdf);
  file->addAction("Append from MangaDx...", this, &MainWindow::appendMangaDx);
  file->addAction("Append from Webtoon...", this, &MainWindow::appendWebtoon);
  file->addSeparator();
  file->addAction("Save project...", this, &MainWindow::saveProject);
  file->addAction("Load project...", this, &MainWindow::loadProject);
  file->addSeparator();
  file->addAction("Export video...", this, &MainWindow::runExport);

  auto *edit = menuBar()->addMenu("&Edit");
  edit->addAction("Characters...", this, &MainWindow::editCharacters);
  edit->addAction("Synopsis...", this, &MainWindow::editSynopsis);
  edit->addAction("Voice (Kokoro)...", this, &MainWindow::editVoice);
  edit->addAction("Page durations...", this, &MainWindow::editDurations);
  edit->addAction("Work Range...", this, &MainWindow::editWorkRange);
  edit->addSeparator();
  edit->addAction("Settings...", this, &MainWindow::editSettings);

  auto *tools = menuBar()->addMenu("&Tools");
  m_toolsMenu = tools;
  tools->addAction("OCR pages", this, &MainWindow::runOcr);
  tools->addAction("Describe pages", this, &MainWindow::runDescribe);
  tools->addAction("Narrate", this, &MainWindow::runNarrate);
  tools->addAction("Synthesize", this, &MainWindow::runSynthesize);

  buildStepBar();
}

// Workflow stepper: the pipeline as 6 glassy stages with live status.
// (Better than the old 15-action toolbar: one glance shows where you are.)
void MainWindow::buildStepBar() {
  m_stepBar = addToolBar("Workflow");
  m_stepBar->setObjectName("stepBar");
  m_stepBar->setMovable(false);
  m_stepBar->setIconSize(QSize(20, 20));
  m_stepBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  auto addStep = [&](const QString &icon, const QString &label, auto slot) {
    QAction *a = m_stepBar->addAction(Icons::get(icon), label);
    a->setStatusTip(label);
    connect(a, &QAction::triggered, this, slot);
  };
  addStep("import", "1 Import", &MainWindow::importFolder);
  addStep("ocr", "2 OCR", &MainWindow::runOcr);
  addStep("describe", "3 Describe", &MainWindow::runDescribe);
  addStep("narrate", "4 Narrate", &MainWindow::runNarrate);
  addStep("voice", "5 Voice", &MainWindow::runSynthesize);
  addStep("export", "6 Export", &MainWindow::runExport);
  auto *spacer = new QWidget(m_stepBar);
  spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  spacer->setStyleSheet("background:transparent;border:none;");
  m_stepBar->addWidget(spacer);
  m_stepStatus = new QLabel("Ready", m_stepBar);
  m_stepStatus->setObjectName("stepStatus");
  m_stepBar->addWidget(m_stepStatus);
}

void MainWindow::setStep(const QString &s) {
  if (m_stepStatus) m_stepStatus->setText(s);
  setStatus(s);
}

void MainWindow::buildDocks() {
  // Left: thumbnail gallery (was a plain text list)
  auto *left = new QDockWidget("Pages", this);
  left->setObjectName("pagesDock");
  m_pagesDock = left;
  left->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
  m_pageList = new QListWidget(left);
  m_pageList->setObjectName("pageGallery");
  m_pageList->setViewMode(QListWidget::IconMode);
  m_pageList->setIconSize(QSize(104, 104));
  m_pageList->setGridSize(QSize(124, 152));
  m_pageList->setMovement(QListView::Static);
  m_pageList->setResizeMode(QListView::Adjust);
  m_pageList->setSpacing(2);
  connect(m_pageList, &QListWidget::currentRowChanged, this, &MainWindow::onPageSelected);
  m_pageList->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(m_pageList, &QWidget::customContextMenuRequested, this, &MainWindow::onPageContextMenu);
  left->setWidget(m_pageList);
  addDockWidget(Qt::LeftDockWidgetArea, left);

  // Center: glass stage card (viewer + cast pills + preview/zoom)
  auto *card = new QFrame(this);
  card->setObjectName("stageCard");
  auto *shadow = new QGraphicsDropShadowEffect(card);
  shadow->setBlurRadius(40);
  shadow->setOffset(0, 10);
  shadow->setColor(QColor(0, 0, 0, 140));
  card->setGraphicsEffect(shadow);
  auto *cl = new QVBoxLayout(card);
  cl->setContentsMargins(12, 12, 12, 12);
  cl->setSpacing(8);
  m_stageScroll = new QScrollArea(card);
  m_stageScroll->setObjectName("stageScroll");
  m_stageScroll->setWidgetResizable(true);
  m_stageScroll->setAlignment(Qt::AlignCenter);
  m_viewer = new QLabel(m_stageScroll);
  m_viewer->setObjectName("stageView");
  m_viewer->setAlignment(Qt::AlignCenter);
  m_stageScroll->setWidget(m_viewer);
  m_stageScroll->viewport()->installEventFilter(this);
  cl->addWidget(m_stageScroll, 1);
  // Stage row: viewer left, description rail right (portrait pages leave
  // the horizontal space empty otherwise; the rail grows vertically).
  auto *stageRow = new QWidget(card);
  stageRow->setStyleSheet("background:transparent;border:none;");
  auto *sr = new QHBoxLayout(stageRow);
  sr->setContentsMargins(0, 0, 0, 0);
  sr->setSpacing(8);
  sr->addWidget(m_stageScroll, 1);
  auto *side = new QWidget(stageRow);
  side->setStyleSheet("background:transparent;border:none;");
  side->setFixedWidth(280);
  auto *sl = new QVBoxLayout(side);
  sl->setContentsMargins(0, 0, 0, 0);
  sl->setSpacing(6);
  auto *descHead = new QWidget(side);
  descHead->setStyleSheet("background:transparent;border:none;");
  auto *dh = new QHBoxLayout(descHead);
  dh->setContentsMargins(0, 0, 0, 0);
  auto *descTitle = new QLabel("Describe the page", descHead);
  descTitle->setObjectName("muted");
  auto *descEditBtn = new QPushButton(descHead);
  descEditBtn->setIcon(Icons::get("edit"));
  descEditBtn->setToolTip("Edit description in a larger editor");
  descEditBtn->setObjectName("ghost");
  descEditBtn->setFixedSize(30, 28);
  connect(descEditBtn, &QPushButton::clicked, this, &MainWindow::editDescription);
  dh->addWidget(descTitle);
  dh->addStretch(1);
  dh->addWidget(descEditBtn);
  sl->addWidget(descHead);
  // Editable visual description (Python's page_desc): what happens on this
  // page in words — critical for textless panels, feeds the Narrate prompt.
  m_descEdit = new QPlainTextEdit(side);
  m_descEdit->setPlaceholderText("Describe this page visually... (used by Narrate, vital for textless panels)");
  m_descEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  connect(m_descEdit, &QPlainTextEdit::textChanged, this, [this]{
    if (m_current >= 0 && m_current < m_project.pages.size())
      m_project.pages[m_current].description = m_descEdit->toPlainText();
  });
  sl->addWidget(m_descEdit, 1);
  sr->addWidget(side);
  cl->addWidget(stageRow, 1);
  m_castLabel = new QLabel("Cast:", card);
  m_castLabel->setObjectName("muted");
  cl->addWidget(m_castLabel);
  m_castRow = new QWidget(card);
  m_castRow->setStyleSheet("background:transparent;border:none;");
  m_castRow->setLayout(new QHBoxLayout());
  cl->addWidget(m_castRow);
  auto *controls = new QWidget(card);
  controls->setStyleSheet("background:transparent;border:none;");
  auto *cr = new QHBoxLayout(controls);
  cr->setContentsMargins(0, 0, 0, 0);
  auto *previewBtn = new QPushButton("Preview page", controls);
  previewBtn->setIcon(Icons::get("play"));
  previewBtn->setObjectName("ghost");
  connect(previewBtn, &QPushButton::clicked, this, [this]{
    if (m_project.audioPath.isEmpty()) { setStatus("No audio yet - step 5 Voice first."); return; }
    m_preview->setSource(QUrl::fromLocalFile(m_project.audioPath));
    m_preview->play();
  });
  auto *zoomOutBtn = new QPushButton("-", controls);
  zoomOutBtn->setObjectName("ghost");
  zoomOutBtn->setFixedWidth(44);
  connect(zoomOutBtn, &QPushButton::clicked, this, &MainWindow::zoomOut);
  m_zoomLabel = new QLabel("100%", controls);
  m_zoomLabel->setObjectName("muted");
  m_zoomLabel->setAlignment(Qt::AlignCenter);
  m_zoomLabel->setFixedWidth(56);
  auto *zoomInBtn = new QPushButton("+", controls);
  zoomInBtn->setObjectName("ghost");
  zoomInBtn->setFixedWidth(44);
  connect(zoomInBtn, &QPushButton::clicked, this, &MainWindow::zoomIn);
  cr->addWidget(previewBtn);
  cr->addStretch(1);
  cr->addWidget(zoomOutBtn);
  cr->addWidget(m_zoomLabel);
  cr->addWidget(zoomInBtn);
  cl->addWidget(controls);
  setCentralWidget(card);

  // Right: inspector with Workflow tab first (run each stage in place)
  auto *right = new QDockWidget("Inspector", this);
  right->setObjectName("inspectorDock");
  right->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
  m_tabs = new QTabWidget(right);
  m_blocksEdit = new QPlainTextEdit(m_tabs);
  m_blocksEdit->setPlaceholderText("OCR blocks for the current page...");
  m_narrationEdit = new QPlainTextEdit(m_tabs);
  m_narrationEdit->setPlaceholderText("Narration script (editable)...");
  m_reviewEdit = new QPlainTextEdit(m_tabs);
  m_reviewEdit->setPlaceholderText("Review notes / story-so-far...");
  m_tabs->addTab(m_blocksEdit, "Blocks");
  m_tabs->addTab(m_narrationEdit, "Narration");
  m_tabs->addTab(m_reviewEdit, "Review");
  right->setWidget(m_tabs);
  addDockWidget(Qt::RightDockWidgetArea, right);

  // Bottom: timeline + transport (play/stop/follow/position)
  auto *bottom = new QDockWidget("Timeline", this);
  bottom->setObjectName("timelineDock");
  auto *tWrap = new QWidget(bottom);
  tWrap->setStyleSheet("background:transparent;border:none;");
  auto *tl = new QVBoxLayout(tWrap);
  tl->setContentsMargins(0, 0, 0, 0);
  tl->setSpacing(4);
  m_timeline = new TimelineWidget(tWrap);
  tl->addWidget(m_timeline, 1);
  auto *transport = new QWidget(tWrap);
  transport->setStyleSheet("background:transparent;border:none;");
  auto *tr = new QHBoxLayout(transport);
  tr->setContentsMargins(2, 0, 2, 2);
  m_playBtn = new QPushButton(transport);
  m_playBtn->setIcon(Icons::get("play"));
  m_playBtn->setToolTip("Play / pause narration audio");
  m_playBtn->setObjectName("ghost");
  m_playBtn->setFixedSize(36, 28);
  connect(m_playBtn, &QPushButton::clicked, this, [this]{ m_timeline->togglePlay(); });
  auto *stopBtn = new QPushButton(transport);
  stopBtn->setIcon(Icons::get("stop"));
  stopBtn->setToolTip("Stop");
  stopBtn->setObjectName("ghost");
  stopBtn->setFixedSize(36, 28);
  connect(stopBtn, &QPushButton::clicked, this, [this]{ m_timeline->stopPlayback(); });
  m_followCheck = new QCheckBox("Follow", transport);
  m_followCheck->setToolTip("Flip stage panels in sync while playing");
  m_followCheck->setChecked(true);
  m_posLabel = new QLabel("0:00 / 0:00", transport);
  m_posLabel->setObjectName("muted");
  tr->addWidget(m_playBtn);
  tr->addWidget(stopBtn);
  tr->addWidget(m_followCheck);
  tr->addStretch(1);
  tr->addWidget(m_posLabel);
  tl->addWidget(transport);
  bottom->setWidget(tWrap);
  addDockWidget(Qt::BottomDockWidgetArea, bottom);

  auto fmtTime = [](double s){
    const int m = int(s) / 60, ss = int(s) % 60;
    return QString("%1:%2").arg(m).arg(ss, 2, 10, QChar('0'));
  };
  connect(m_timeline, &TimelineWidget::positionChanged, this,
          [this, fmtTime](double sec, double total){
            m_posLabel->setText(fmtTime(sec) + " / " + fmtTime(total));
          });
  connect(m_timeline, &TimelineWidget::playbackToggled, this, [this](bool playing){
    m_playBtn->setIcon(Icons::get(playing ? "pause" : "play"));
  });
  // Follow-playback: flip the stage to whatever page is speaking.
  connect(m_timeline, &TimelineWidget::pageChanged, this, [this](int idx){
    if (!m_followCheck || !m_followCheck->isChecked()) return;
    if (!m_timeline->isPlaying()) return;
    auto [from, to] = m_project.workRange();
    const int pageIdx = (from - 1) + idx; // timeline pages are range-relative
    if (pageIdx >= 0 && pageIdx < m_project.pages.size())
      showPage(pageIdx);
  });

  resizeDocks({left, right}, {176, 340}, Qt::Horizontal);
  statusBar()->showMessage("Ready");
}

void MainWindow::setStatus(const QString &s, int pct) {
  statusBar()->showMessage(s);
  // ONE persistent bar — creating one per update stacked them across the bar.
  if (!m_progress) {
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setMaximumWidth(200);
    m_progress->setTextVisible(true);
    statusBar()->addPermanentWidget(m_progress);
    m_progress->hide();
  }
  if (pct < 0) {
    m_progress->hide();
  } else {
    m_progress->show();
    m_progress->setValue(qBound(0, pct, 100));
    if (pct >= 100) m_progress->hide();
  }
}

// While a workflow step runs, its siblings lock so two heavy jobs can't
// overlap (and a locked step can't be re-triggered by double click).
void MainWindow::setBusy(bool busy) {
  m_busyCount = qMax(0, m_busyCount + (busy ? 1 : -1));
  const bool free = (m_busyCount == 0);
  if (m_stepBar)
    for (QAction *a : m_stepBar->actions()) a->setEnabled(free);
  if (m_toolsMenu)
    for (QAction *a : m_toolsMenu->actions()) a->setEnabled(free);
}

void MainWindow::closeEvent(QCloseEvent *ev) {
  if (m_busyCount > 0) {
    auto pick = QMessageBox::question(
      this, "Workflow running",
      "A workflow step is still running. Closing now will abandon it.\n"
      "Close anyway?",
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (pick != QMessageBox::Yes) { ev->ignore(); return; }
  }
  autosave();
  QMainWindow::closeEvent(ev);
}

// ---- Import ----

void MainWindow::setProjectPages(const QString &name, const QStringList &images) {
  m_project = NS::Project{};
  m_project.name = name.isEmpty() ? "Untitled" : name;
  int n = 1;
  for (const auto &p : images) {
    NS::Page pg; pg.number = n++; pg.imagePath = p;
    m_project.pages.append(pg);
  }
  m_project.workEnd = 0;
  refreshPageList();
  if (!m_project.pages.isEmpty()) showPage(0);
  updatePagesTitle();
  autosave();
}

void MainWindow::importFolder() {
  QString dir = QFileDialog::getExistingDirectory(this, "Import image folder");
  if (dir.isEmpty()) return;
  QStringList imgs = NS::Pipeline::collectImages(dir);
  if (imgs.isEmpty()) { setStatus("No images found in folder."); return; }
  setProjectPages(QDir(dir).dirName(), imgs);
  setStatus(QString("Imported %1 pages.").arg(imgs.size()));
}

void MainWindow::importPdf() {
  const QString pdf = QFileDialog::getOpenFileName(this, "Import PDF", "", "PDF (*.pdf)");
  if (pdf.isEmpty()) return;
  setStep("Step 1/6 | Rendering PDF pages...");
  auto *task = new Task([this, pdf](auto progress, auto cancelled) {
    Q_UNUSED(cancelled);
    QString err;
    QStringList imgs = NS::Pipeline::renderPdfPages(
      pdf, 2048,
      [&](int done, int total){ progress(done * 100 / qMax(1, total), "Rendering PDF..."); },
      &err);
    if (imgs.isEmpty())
      throw std::runtime_error(err.isEmpty() ? "PDF render failed" : err.toStdString());
    QMetaObject::invokeMethod(this, [this, imgs, pdf]{
      setProjectPages(QFileInfo(pdf).baseName(), imgs);
      setStep(QString("Step 1/6 | Imported %1 pages from PDF.").arg(imgs.size()));
    }, Qt::QueuedConnection);
    progress(100, "PDF import done.");
  });
  connect(task, &Task::progress, this, [this](int p, const QString &s){ setStatus(s, p); });
  connect(task, &Task::error, this, [this](const QString &e){ setStatus("PDF failed: " + e); });
  connect(task, &Task::done, this, [this]{ setBusy(false); });
  connect(task, &Task::error, this, [this](const QString &){ setBusy(false); });
  setBusy(true);
  QThreadPool::globalInstance()->start(task);
}

void MainWindow::appendProjectPages(const QStringList &images) {
  if (images.isEmpty()) { setStatus("Nothing new to append."); return; }
  if (m_project.pages.isEmpty()) {
    setProjectPages(QDir(images.first()).dirName(), images);
    return;
  }
  const int firstNew = m_project.pages.last().number + 1;
  int n = firstNew;
  for (const auto &p : images) {
    NS::Page pg; pg.number = n++; pg.imagePath = p;
    m_project.pages.append(pg);
  }
  m_project.workStart = firstNew;
  m_project.workEnd = n - 1;
  m_settings.setWorkStart(firstNew);
  m_settings.setWorkEnd(n - 1);
  refreshPageList();
  showPage(m_project.pages.size() - 1);
  updatePagesTitle();
  setStep(QString("Step 1/6 | Appended %1 pages (%2-%3) - work range set to new pages.")
          .arg(images.size()).arg(firstNew).arg(n - 1));
  autosave();
}

void MainWindow::appendFolder() {
  QString dir = QFileDialog::getExistingDirectory(this, "Append image folder");
  if (dir.isEmpty()) return;
  appendProjectPages(NS::Pipeline::collectImages(dir));
}

void MainWindow::appendPdf() {
  const QString pdf = QFileDialog::getOpenFileName(this, "Append PDF", "", "PDF (*.pdf)");
  if (pdf.isEmpty()) return;
  setStep("Step 1/6 | Rendering PDF pages to append...");
  auto *task = new Task([this, pdf](auto progress, auto cancelled) {
    Q_UNUSED(cancelled);
    QString err;
    QStringList imgs = NS::Pipeline::renderPdfPages(
      pdf, 2048,
      [&](int done, int total){ progress(done * 100 / qMax(1, total), "Rendering PDF..."); }, &err);
    if (imgs.isEmpty())
      throw std::runtime_error(err.isEmpty() ? "PDF render failed" : err.toStdString());
    QMetaObject::invokeMethod(this, [this, imgs, progress]{
      appendProjectPages(imgs);
      progress(100, "PDF appended.");
    }, Qt::BlockingQueuedConnection);
  });
  connect(task, &Task::progress, this, [this](int p, const QString &s){ setStatus(s, p); });
  connect(task, &Task::error, this, [this](const QString &e){ setStatus("PDF failed: " + e); });
  connect(task, &Task::done, this, [this]{ setBusy(false); });
  connect(task, &Task::error, this, [this](const QString &){ setBusy(false); });
  setBusy(true);
  QThreadPool::globalInstance()->start(task);
}

void MainWindow::appendMangaDx() {
  MangaDxService svc(this);
  MangaDxDialog::Result res;
  MangaDxDialog dlg(&svc, res, this);
  if (dlg.exec() != QDialog::Accepted || res.images.isEmpty()) return;
  appendProjectPages(res.images);
}

void MainWindow::appendWebtoon() {
  WebtoonDialog::Result wr;
  WebtoonDialog dlg(wr, this);
  if (dlg.exec() != QDialog::Accepted || wr.url.isEmpty()) return;
  setStep("Step 1/6 | Downloading Webtoon episodes to append...");
  auto *task = new Task([this, wr](auto progress, auto cancelled) {
    Q_UNUSED(cancelled);
    WebtoonService svc;
    QString err;
    QStringList imgs = svc.download(wr.url, {}, wr.startChapter, wr.endChapter,
                                    wr.latestOnly, &err);
    if (imgs.isEmpty()) throw std::runtime_error(
      err.isEmpty() ? "no episodes downloaded" : err.toStdString());
    QMetaObject::invokeMethod(this, [this, imgs]{
      appendProjectPages(imgs);
    }, Qt::BlockingQueuedConnection);
    progress(100, "Webtoon import done.");
  });
  connect(task, &Task::progress, this, [this](int p, const QString &s){ setStatus(s, p); });
  connect(task, &Task::error, this, [this](const QString &e){ setStatus("Webtoon failed: " + e); });
  connect(task, &Task::done, this, [this]{ setBusy(false); });
  connect(task, &Task::error, this, [this](const QString &){ setBusy(false); });
  setBusy(true);
  QThreadPool::globalInstance()->start(task);
}

void MainWindow::importMangaDx() {
  MangaDxService svc(this);
  MangaDxDialog::Result res;
  MangaDxDialog dlg(&svc, res, this);
  if (dlg.exec() != QDialog::Accepted || res.images.isEmpty()) return;
  setProjectPages(res.title.isEmpty() ? "MangaDx import" : res.title, res.images);
  setStep(QString("Step 1/6 | Imported %1 pages from MangaDx.").arg(res.images.size()));
}

void MainWindow::importWebtoon() {
  WebtoonDialog::Result wr;
  WebtoonDialog dlg(wr, this);
  if (dlg.exec() != QDialog::Accepted || wr.url.isEmpty()) return;
  setStep("Step 1/6 | Downloading Webtoon episodes...");
  auto *task = new Task([this, wr](auto progress, auto cancelled) {
    Q_UNUSED(cancelled);
    WebtoonService svc;
    QString err;
    QStringList imgs = svc.download(wr.url, {}, wr.startChapter, wr.endChapter,
                                    wr.latestOnly, &err);
    if (imgs.isEmpty()) throw std::runtime_error(
      err.isEmpty() ? "no episodes downloaded" : err.toStdString());
    const QString url = wr.url;
    QMetaObject::invokeMethod(this, [this, imgs, url]{
      setProjectPages(url, imgs);
      setStep(QString("Step 1/6 | Imported %1 pages from Webtoon.").arg(imgs.size()));
    }, Qt::QueuedConnection);
    progress(100, "Webtoon import done.");
  });
  connect(task, &Task::progress, this, [this](int p, const QString &s){ setStatus(s, p); });
  connect(task, &Task::error, this, [this](const QString &e){ setStatus("Webtoon failed: " + e); });
  connect(task, &Task::done, this, [this]{ setBusy(false); });
  connect(task, &Task::error, this, [this](const QString &){ setBusy(false); });
  setBusy(true);
  QThreadPool::globalInstance()->start(task);
}

// ---- Heavy steps: thread-pool + zero-copy decode ----

void MainWindow::runOcr() {
  auto [from, to] = m_project.workRange();
  QString reason;
  if (!RapidOcrEngine::available(&reason)) { setStatus("OCR: " + reason); return; }
  setStep(QString("Step 2/6 | OCR pages %1-%2...").arg(from).arg(to));
  struct Job { int pageIdx; QString path; };
  QVector<Job> jobs;
  for (qsizetype i = 0; i < m_project.pages.size(); ++i) {
    const auto &pg = m_project.pages[i];
    if (pg.number >= from && pg.number <= to)
      jobs.append({static_cast<int>(i), pg.imagePath});
  }
  if (jobs.isEmpty()) { setStatus("Work range has no pages."); return; }
  auto *task = new Task([this, jobs](auto progress, auto cancelled) {
    RapidOcrEngine eng; // sessions init once, reused per page
    int totalBlocks = 0;
    for (qsizetype j = 0; j < jobs.size(); ++j) {
      if (cancelled()) return;
      progress(static_cast<int>(j * 100 / jobs.size()),
               QString("OCR page %1/%2...").arg(j + 1).arg(jobs.size()));
      QString err;
      auto out = eng.recognize({jobs[j].path}, &err);
      if (!err.isEmpty() && out.isEmpty())
        throw std::runtime_error(err.toStdString());
      const int idx = jobs[j].pageIdx;
      const auto blocks = out.isEmpty() ? QVector<NS::TextBlock>{} : out.first();
      totalBlocks += blocks.size();
      QMetaObject::invokeMethod(this, [this, idx, blocks]{
        if (idx >= 0 && idx < m_project.pages.size())
          m_project.pages[idx].blocks = blocks;
      }, Qt::BlockingQueuedConnection);
    }
    QMetaObject::invokeMethod(this, [this, totalBlocks]{
      showPage(m_current);
      setStep(QString("Step 2/6 | OCR done: %1 blocks.").arg(totalBlocks));
      autosave();
    }, Qt::QueuedConnection);
    progress(100, "OCR done.");
  });
  connect(task, &Task::progress, this, [this](int p, const QString &s){ setStatus(s, p); });
  connect(task, &Task::error, this, [this](const QString &e){ setStatus("OCR failed: " + e); });
  connect(task, &Task::done, this, [this]{ setBusy(false); });
  connect(task, &Task::error, this, [this](const QString &){ setBusy(false); });
  setBusy(true);
  QThreadPool::globalInstance()->start(task);
}

void MainWindow::runDescribe() {
  auto [from, to] = m_project.workRange();
  QString reason;
  const bool useFlorence = FlorenceCaptionEngine::available(&reason);
  if (!useFlorence && m_project.pages.isEmpty()) { setStatus("Nothing to describe."); return; }
  setStep(useFlorence ? QString("Step 3/6 | Describing pages %1-%2 with Florence-2...").arg(from).arg(to)
                      : QString("Step 3/6 | Describing pages %1-%2... (%1)").arg(from).arg(to).arg(reason));
  struct Job { int pageIdx; QString path; };
  QVector<Job> jobs;
  for (qsizetype i = 0; i < m_project.pages.size(); ++i) {
    const auto &pg = m_project.pages[i];
    if (pg.number < from || pg.number > to) continue;
    if (!pg.description.isEmpty()) continue; // keep user-written notes
    jobs.append({static_cast<int>(i), pg.imagePath});
  }
  if (jobs.isEmpty()) { setStatus("Nothing to describe (pages already have notes)."); return; }
  auto *task = new Task([this, jobs, useFlorence](auto progress, auto cancelled) {
    FlorenceCaptionEngine eng; // sessions init once, reused per page
    int done = 0;
    for (qsizetype j = 0; j < jobs.size(); ++j) {
      if (cancelled()) return;
      progress(static_cast<int>(j * 100 / jobs.size()),
               QString("Describing page %1/%2...").arg(j + 1).arg(jobs.size()));
      QString desc;
      if (useFlorence) {
        QString err;
        desc = eng.describe(jobs[j].path, &err);
        if (!err.isEmpty() && desc.isEmpty())
          throw std::runtime_error(err.toStdString());
      } else {
        desc = "(auto-describe stub - plug Florence-2 ONNX here)";
      }
      const int idx = jobs[j].pageIdx;
      QMetaObject::invokeMethod(this, [this, idx, desc]{
        if (idx >= 0 && idx < m_project.pages.size()
            && m_project.pages[idx].description.isEmpty())
          m_project.pages[idx].description = desc;
      }, Qt::BlockingQueuedConnection);
      ++done;
    }
    QMetaObject::invokeMethod(this, [this, done]{
      showPage(m_current);
      setStep(QString("Step 3/6 | Described %1 pages.").arg(done));
      autosave();
    }, Qt::QueuedConnection);
    progress(100, "Describe done.");
  });
  connect(task, &Task::progress, this, [this](int p, const QString &s){ setStatus(s, p); });
  connect(task, &Task::error, this, [this](const QString &e){ setStatus("Describe failed: " + e); });
  connect(task, &Task::done, this, [this]{ setBusy(false); });
  connect(task, &Task::error, this, [this](const QString &){ setBusy(false); });
  setBusy(true);
  QThreadPool::globalInstance()->start(task);
}

void MainWindow::runNarrate() {
  auto [from, to] = m_project.workRange();
  if (m_project.pages.isEmpty()) { setStatus("Import pages first."); return; }
  // Backend priority mirrors Python: cloud key > Gemini key > local Ollama.
  // No keys configured → instant stub so the flow stays demoable.
  NS::NarrateBackend be;
  be.label = "stub";
  be.maxPromptTokens = 6000;
  be.models = {"stub"};
  const QStringList llmKeys = m_settings.llmKeys();
  const QStringList gemKeys = m_settings.geminiKeys();
  if (!llmKeys.isEmpty()) {
    be.label = QString("%1 (%2 key(s))").arg(m_settings.llmModel()).arg(llmKeys.size());
    be.maxPromptTokens = 4000;
    be.models = {m_settings.llmModel()};
    be.apiKeys = llmKeys;
  } else if (!gemKeys.isEmpty()) {
    be.label = QString("%1 (%2 key(s))").arg(m_settings.geminiModel()).arg(gemKeys.size());
    be.maxPromptTokens = 30000; // huge context: ~100-page chunks, few requests
    be.models = {m_settings.geminiModel()};
    for (const QString &m : m_settings.geminiFallbacks().split(',', Qt::SkipEmptyParts)) {
      const QString t = m.trimmed();
      if (!t.isEmpty() && !be.models.contains(t)) be.models << t;
    }
    be.apiKeys = gemKeys;
  } else {
    be.label = "Ollama " + m_settings.ollamaModel();
    be.maxPromptTokens = 16000;
    be.models = {m_settings.ollamaModel()};
  }
  const QString kind = be.models.first() == "stub" ? "stub"
    : (!llmKeys.isEmpty() ? "openai" : (!gemKeys.isEmpty() ? "gemini" : "ollama"));
  const QString base = kind == "openai" ? m_settings.llmBase()
    : kind == "gemini" ? QString("https://generativelanguage.googleapis.com/v1beta/openai")
    : m_settings.ollamaUrl();
  if (kind == "stub")
    be.label = "stub (add a key in Edit > Settings for real narration)";
  setStep(QString("Step 4/6 | Narrating %1-%2 with %3...").arg(from).arg(to).arg(be.label));
  NS::NarrateLimits limits;
  limits.chunkPages = qMax(1, m_settings.chunkPages());
  limits.tokensPerMinute = qMax(1, m_settings.tokensPerMinute());
  limits.requestsPerMinute = qMax(1, m_settings.requestsPerMinute());
  // Story-so-far: explicit review notes win, else the existing narration tail
  // keeps multi-part continuations seamless (Python's prior_story).
  const QString review = m_reviewEdit->toPlainText();
  const QString prior = !review.trimmed().isEmpty() ? review : m_project.narration;

  auto *task = new Task([=](auto progress, auto cancelled) {
    NS::NarrateBackend runBe = be;
    if (kind == "stub") {
      runBe.request = [](const QString &, const QString &, const QString &prompt) {
        StubNarrator stub;
        return stub.narrate(prompt);
      };
    } else if (kind == "ollama") {
      runBe.request = [base](const QString &model, const QString &, const QString &prompt) {
        OllamaNarrator nar(base, model);
        QString err;
        QString out = nar.narrate(prompt, &err);
        if (!err.isEmpty() && out.trimmed().isEmpty())
          throw std::runtime_error(err.toStdString());
        return out;
      };
    } else {
      runBe.request = [base](const QString &model, const QString &key, const QString &prompt) {
        OpenAiCompatNarrator nar(base, key, model);
        QString err;
        QString out = nar.narrate(prompt, &err);
        if (!err.isEmpty() && out.trimmed().isEmpty())
          throw std::runtime_error(err.toStdString());
        return out;
      };
    }
    NS::NarrateResult res = NS::narrateChapter(
      m_project, from, to, prior, runBe, limits,
      [&](int pct, const QString &s){ progress(pct, s); },
      [&](){ return cancelled(); });
    // Continuation: a range starting past all narrated pages appends to the
    // existing script (absolute segment offsets); anything else replaces it.
    int maxSegPage = 0;
    QVector<NS::PageSegment> oldSegs;
    for (auto &s : m_project.segments) {
      maxSegPage = qMax(maxSegPage, s.page);
      oldSegs.append({s.page, s.start, s.end});
    }
    QPair<QString, QVector<NS::PageSegment>> merged;
    if (from > maxSegPage && !m_project.narration.trimmed().isEmpty())
      merged = NS::appendScript(m_project.narration, oldSegs, res.script, res.segments);
    else
      merged = {res.script, res.segments};
    QMetaObject::invokeMethod(m_narrationEdit, [this, merged]{
      m_narrationEdit->setPlainText(merged.first);
      m_project.narration = merged.first;
      m_project.segments.clear();
      for (auto &s : merged.second)
        m_project.segments.append({s.page, s.start, s.end});
    }, Qt::BlockingQueuedConnection);
    if (res.failed)
      throw std::runtime_error(
        (res.error + QString(" (kept pages %1-%2 narrated so far.)")
         .arg(from).arg(res.lastDone)).toStdString());
    progress(100, QString("Narrated pages %1-%2 (%3 segments).")
             .arg(from).arg(to).arg(res.segments.size()));
  });
  connect(task, &Task::progress, this, [this](int p, const QString &s){ setStatus(s, p); });
  connect(task, &Task::done, this, [this]{ autosave(); });
  connect(task, &Task::done, this, [this]{ setBusy(false); });
  connect(task, &Task::error, this, [this](const QString &e){ setStatus("Narrate failed: " + e); });
  connect(task, &Task::error, this, [this](const QString &){ setBusy(false); });
  setBusy(true);
  QThreadPool::globalInstance()->start(task);
}

// Write raw mono-16-bit PCM with a WAV header.
static bool writePcmWav(const QString &path, const QByteArray &pcm, QString *error) {  QFile o(path);
  if (!o.open(QIODevice::WriteOnly)) {
    if (error) *error = "Cannot write " + path;
    return false;
  }
  QByteArray hdr(44, 0);
  auto w32 = [&](int off, quint32 v){
    hdr[off] = char(v & 0xff); hdr[off+1] = char((v>>8)&0xff);
    hdr[off+2] = char((v>>16)&0xff); hdr[off+3] = char((v>>24)&0xff); };
  auto w16 = [&](int off, quint16 v){
    hdr[off] = char(v & 0xff); hdr[off+1] = char((v>>8)&0xff); };
  memcpy(hdr.data(), "RIFF", 4);
  w32(4, 36 + quint32(pcm.size())); memcpy(hdr.data()+8, "WAVE", 4);
  memcpy(hdr.data()+12, "fmt ", 4); w32(16, 16); w16(20, 1); w16(22, 1);
  w32(24, 24000); w32(28, 24000*2); w16(32, 2); w16(34, 16);
  memcpy(hdr.data()+36, "data", 4); w32(40, quint32(pcm.size()));
  o.write(hdr); o.write(pcm); o.close();
  return true;
}

// _promoteWav: stop players holding the old file, rename tmp over wav,
// else a fresh versioned name. Never fails the run over a locked file.
static QString promoteWav(const QString &tmp, const QString &wav,
                          std::function<void()> stopPlayers) {
  stopPlayers();
  QFile::remove(wav);
  if (QFile::rename(tmp, wav)) return wav;
  QFile::remove(wav);
  if (QFile::copy(tmp, wav)) { QFile::remove(tmp); return wav; }
  for (int v = 2; v < 100; ++v) {
    QString alt = QDir::current().absoluteFilePath(
      QString("temp/narration_%1.wav").arg(v));
    QFile::remove(alt);
    if (QFile::rename(tmp, alt)) return alt;
  }
  throw std::runtime_error("Cannot promote narration.wav - is it open in another app?");
}

static double wavSeconds(const QString &path);

void MainWindow::runSynthesize() {
  auto [from, to] = m_project.workRange();
  QString script = m_narrationEdit->toPlainText();
  m_project.narration = script;
  if (m_project.pages.isEmpty()) { setStatus("Import pages first."); return; }
  if (script.trimmed().isEmpty()) { setStatus("Nothing to synthesize - Narrate first."); return; }
  // Python parity: tagged scripts voice per page with measured boundaries;
  // untagged/edited scripts voice whole (never chop words) with proportional
  // markers estimated from the total.
  QVector<NS::PageSegment> segs;
  for (auto &s : m_project.segments) segs.append({s.page, s.start, s.end});
  const bool measured = NS::narrationSegmentsValid(script, segs, from, to);
  const QString kokoroVoice = m_settings.speechVoice();
  const double kokoroSpeed = m_settings.speechSpeed();
  QString reason;
  const bool useKokoro = KokoroTtsEngine::available(&reason);
  const int rangeSize = to - from + 1;
  setStep(useKokoro ? QString("Step 5/6 | Voicing pages %1-%2 with Kokoro %3%4...").arg(from).arg(to).arg(kokoroVoice).arg(measured ? "" : " (seamless, estimated sync)")
                    : QString("Step 5/6 | Voicing pages %1-%2... (%3)").arg(from).arg(to).arg(reason));
  auto *task = new Task([=](auto progress, auto cancelled) {
    Q_UNUSED(cancelled);
    progress(2, QString("Loading voice for pages %1-%2...").arg(from).arg(to));
    // Kokoro when its files resolve, silence stub otherwise (timeline keeps working).
    std::unique_ptr<ITtsEngine> tts(
      useKokoro ? static_cast<ITtsEngine *>(new KokoroTtsEngine(kokoroVoice, kokoroSpeed))
                : static_cast<ITtsEngine *>(new StubTtsEngine()));
    QString wav = QDir::current().absoluteFilePath("temp/narration.wav");
    QDir().mkpath("temp");
    auto stopPlayers = [&]{
      QMetaObject::invokeMethod(this, [this]{
        if (m_timeline) m_timeline->stopPlayback();
        if (m_preview) { m_preview->stop(); m_preview->setSource(QUrl()); }
      }, Qt::BlockingQueuedConnection);
    };
    QVector<double> markers;
    QString finalWav;
    if (measured) {
      // Tagged script: per-page audio, measured page boundaries.
      QVector<QString> slices = NS::pageSlices(script, segs, from, to);
      const QString vdir = QDir::current().absoluteFilePath("temp/voice");
      QStringList wavs;
      QString err;
      if (!tts->synthesizePages(slices, vdir, &wavs, nullptr,
                                [&](int i, int n){
                                  const int pg = from + i;
                                  progress(5 + i * 90 / qMax(1, n),
                                           QString("Voice: page %1 of %2-%3...").arg(pg).arg(from).arg(to));
                                }, &err)) {
        throw std::runtime_error(err.toStdString());
      }
      QByteArray pcmAll;
      double total = 0;
      for (int wi = 0; wi < wavs.size(); ++wi) {
        const QString &wp = wavs[wi];
        QFile f(wp);
        if (!f.open(QIODevice::ReadOnly))
          throw std::runtime_error(("Missing page audio: " + wp).toStdString());
        QByteArray blob = f.readAll();
        if (blob.size() <= 44)
          throw std::runtime_error(("Bad page audio: " + wp).toStdString());
        const int frames = (blob.size() - 44) / 2; // mono 16-bit
        if (wi > 0) markers << total; // pages 2..N boundaries only
        total += frames / 24000.0;
        pcmAll.append(blob.constData() + 44, blob.size() - 44);
      }
      const QString tmp = wav + ".new";
      if (!writePcmWav(tmp, pcmAll, &err))
        throw std::runtime_error(err.toStdString());
      finalWav = promoteWav(tmp, wav, stopPlayers);
    } else {
      // Untagged/edited script: ONE seamless render (words never chopped),
      // markers estimated proportionally (Python parity).
      progress(10, "Voicing full narration seamlessly...");
      const QString tmp = wav + ".new";
      QString err;
      if (!tts->synthesize(script, tmp, nullptr, &err))
        throw std::runtime_error(err.toStdString());
      finalWav = promoteWav(tmp, wav, stopPlayers);
      const double total = wavSeconds(finalWav);
      for (int i = 1; i < rangeSize; ++i)
        markers << total * i / rangeSize;
    }
    QMetaObject::invokeMethod(m_timeline, [=]{
      m_project.audioPath = finalWav;
      m_project.markers = markers;
      m_timeline->setAudio(finalWav);
      m_timeline->setMarkers(markers);
      m_timeline->setPageCount(rangeSize);
    }, Qt::QueuedConnection);
    progress(100, measured
             ? QString("Voiced pages %1-%2 (%3 markers).").arg(from).arg(to).arg(markers.size())
             : QString("Voiced pages %1-%2 seamlessly (%3 estimated markers).").arg(from).arg(to).arg(markers.size()));
  });
  connect(task, &Task::progress, this, [this](int p, const QString &s){ setStatus(s, p); });
  connect(task, &Task::done, this, [this]{ autosave(); reviewNarrations(); });
  connect(task, &Task::done, this, [this]{ setBusy(false); });
  connect(task, &Task::error, this, [this](const QString &e){ setStatus("Voice failed: " + e); });
  connect(task, &Task::error, this, [this](const QString &){ setBusy(false); });
  setBusy(true);
  QThreadPool::globalInstance()->start(task);
}

static double wavSeconds(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly) || f.size() <= 44) return 0;
  QByteArray hdr = f.read(44);
  const uchar *p = reinterpret_cast<const uchar *>(hdr.constData());
  auto r32 = [&](int o){ return quint32(p[o]) | (quint32(p[o+1])<<8) | (quint32(p[o+2])<<16) | (quint32(p[o+3])<<24); };
  const quint32 rate = r32(24), bytes = r32(40);
  if (rate < 1000 || bytes == 0) return 0;
  return double(bytes) / 2 / rate; // mono 16-bit
}

void MainWindow::runExport() {
  if (m_project.pages.isEmpty()) { setStatus("Nothing to export."); return; }
  auto [from, to] = m_project.workRange();
  QString ff;
  if (!VideoService::haveFfmpeg(&ff)) {
    setStatus("Export needs ffmpeg - run scripts/fetch-ffmpeg.ps1, or install ffmpeg on PATH.");
    return;
  }
  // Count excluded pages
  int excludedCount = 0;
  for (auto &pg : m_project.pages)
    if (pg.number >= from && pg.number <= to && pg.excluded) excludedCount++;
  if (excludedCount > 0) {
    QMessageBox box(this);
    box.setWindowTitle("Export with Excluded Pages");
    box.setIcon(QMessageBox::Information);
    box.setText(QString("%1 page(s) will be excluded from export.\nContinue?").arg(excludedCount));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    if (box.exec() != QMessageBox::Yes) return;
  }
  ExportDialog::Result r;
  ExportDialog dlg(r, this);
  if (dlg.exec() != QDialog::Accepted) return;
  QString out = QFileDialog::getSaveFileName(this, "Export MP4", "output.mp4", "MP4 (*.mp4)");
  if (out.isEmpty()) return;
  // Filter excluded pages: build image list and collect excluded indices
  QStringList imgs;
  QSet<int> excludedIndices;
  int imgIdx = 0;
  for (auto &pg : m_project.pages) {
    if (pg.number >= from && pg.number <= to) {
      if (pg.excluded) {
        excludedIndices.insert(imgIdx);
      } else {
        imgs << pg.imagePath;
      }
      imgIdx++;
    }
  }
  if (imgs.isEmpty()) { setStatus("All pages in work range are excluded - nothing to export."); return; }
  // Handle audio: if excluded pages have audio, splice it out
  QString audioPath = m_project.audioPath;
  if (!excludedIndices.isEmpty() && !audioPath.isEmpty() && QFile::exists(audioPath)) {
    // Slice the master WAV into per-page segments, then merge non-excluded ones
    const QString sliceDir = QDir::current().absoluteFilePath("temp/slices");
    QStringList pageWavs = VideoService::sliceAudio(audioPath, m_project.markers, sliceDir);
    if (!pageWavs.isEmpty()) {
      const QString merged = QDir::current().absoluteFilePath("temp/export_audio.wav");
      QString mergedPath = VideoService::mergeAudio(pageWavs, excludedIndices, merged);
      if (!mergedPath.isEmpty()) audioPath = mergedPath;
    }
  }
  const double total = wavSeconds(audioPath);
  if (total <= 0)
    setStatus("Note: no voiced audio - exporting silent video at 5s per page.");
  QVector<double> durs = NS::Timeline::markersToDurations(
    m_timeline->markers(), total > 0 ? total : 5.0 * imgs.size(), imgs.size());
  VideoService::Options opt;
  opt.width = r.width; opt.height = r.height; opt.backgroundMode = r.bgMode;
  opt.backgroundImage = r.bgImage;
  // Hour-long exports must be cancellable: temporary status-bar button wired
  // to the task's cooperative cancel flag.
  auto *cancelBtn = new QPushButton("Cancel export", this);
  cancelBtn->setObjectName("ghost");
  statusBar()->addPermanentWidget(cancelBtn);
  setStep(QString("Step 6/6 | Exporting %1 pages (%2 excluded)...").arg(imgs.size()).arg(excludedCount));
  auto *task = new Task([=](auto progress, auto cancelled) {
    VideoService vs;
    QString err;
    QString res = vs.exportSlideshow(imgs, audioPath, out, durs, opt, &err,
      [&](int pct, const QString &s){ progress(pct, s); },
      [&](){ return cancelled(); });
    if (res.isEmpty()) throw std::runtime_error(err.toStdString());
    QMetaObject::invokeMethod(this, [this, res, imgs]{
      setStatus(QString("Exported %1 pages to %2").arg(imgs.size()).arg(res));
    }, Qt::QueuedConnection);
    progress(100, "Export done.");
  });
  connect(cancelBtn, &QPushButton::clicked, task, &Task::requestCancel);
  auto dropCancel = [this, cancelBtn]{
    statusBar()->removeWidget(cancelBtn);
    cancelBtn->deleteLater();
  };
  connect(task, &Task::progress, this, [this](int p, const QString &s){ setStatus(s, p); });
  connect(task, &Task::done, this, [this, dropCancel]{ dropCancel(); setBusy(false); });
  connect(task, &Task::error, this, [this, dropCancel](const QString &e){ dropCancel(); setStatus("Export failed: " + e); });
  connect(task, &Task::error, this, [this](const QString &){ setBusy(false); });
  setBusy(true);
  QThreadPool::globalInstance()->start(task);
}

// ---- Project / UI plumbing ----

void MainWindow::saveProject() {
  QString path = QFileDialog::getSaveFileName(this, "Save project", "projects/project.db", "SQLite (*.db)");
  if (path.isEmpty()) return;
  m_narrationEdit->toPlainText();  // flush
  m_project.narration = m_narrationEdit->toPlainText();
  QString err;
  setStatus(NS::ProjectStore::save(path, m_project, &err) ? ("Saved " + path) : ("Save failed: " + err));
}

void MainWindow::loadProject() {
  QString path = QFileDialog::getOpenFileName(this, "Load project", "projects", "SQLite (*.db)");
  if (path.isEmpty()) return;
  NS::Project p; QString err;
  if (!NS::ProjectStore::load(path, p, &err)) { setStatus("Load failed: " + err); return; }
  m_project = p;
  m_narrationEdit->setPlainText(p.narration);
  m_settings.setWorkStart(p.workStart);
  m_settings.setWorkEnd(p.workEnd);
  refreshPageList();
  if (!p.pages.isEmpty()) showPage(0);
  updatePagesTitle();
  // Restore voiced state: timeline audio + measured markers, so export
  // right after load carries sound with exact sync.
  if (!p.audioPath.isEmpty() && QFile::exists(p.audioPath)) {
    m_timeline->setAudio(p.audioPath);
    m_timeline->setMarkers(p.markers);
    auto [rFrom, rTo] = m_project.workRange();
    m_timeline->setPageCount(qMax(1, rTo - rFrom + 1));
    setStatus("Loaded " + path + " (audio restored).");
  } else {
    if (!p.audioPath.isEmpty())
      setStatus("Loaded " + path + " (saved audio file is gone - re-run Voice).");
    else
      setStatus("Loaded " + path);
  }
}

void MainWindow::editCharacters() {
  CharactersDialog dlg(m_project.characters, this);
  dlg.exec();
  showPage(m_current);
}

void MainWindow::editSynopsis() {
  SynopsisDialog dlg(m_project.synopsis, this);
  dlg.exec();
}

void MainWindow::editDescription() {
  if (m_current < 0 || m_current >= m_project.pages.size()) return;
  auto &pg = m_project.pages[m_current];
  DescriptionDialog dlg(pg.number, pg.description, this);
  if (dlg.exec() != QDialog::Accepted) return;
  {
    const QSignalBlocker block(m_descEdit);
    m_descEdit->setPlainText(pg.description);
  }
  autosave();
}

void MainWindow::editVoice() {
  VoiceDialog dlg(m_settings, this);
  dlg.exec();
}

void MainWindow::editSettings() {
  SettingsDialog dlg(m_settings, this);
  dlg.exec();
}

void MainWindow::editWorkRange() {
  if (m_project.pages.isEmpty()) { setStatus("Import pages first."); return; }
  auto [curFrom, curTo] = m_project.workRange();
  WorkRangeDialog dlg(m_project.pageCount(), curFrom,
                      m_project.workEnd <= 0 ? 0 : curTo, this);
  if (dlg.exec() != QDialog::Accepted) return;
  m_project.workStart = dlg.fromPage();
  m_project.workEnd = dlg.toPage(); // <= 0 = all pages
  m_settings.setWorkStart(m_project.workStart);
  m_settings.setWorkEnd(m_project.workEnd);
  updatePagesTitle();
  if (m_project.workEnd <= 0)
    setStatus(QString("Work range: all %1 pages.").arg(m_project.pageCount()));
  else
    setStatus(QString("Work range: pages %1-%2 of %3.")
              .arg(m_project.workStart).arg(m_project.workEnd)
              .arg(m_project.pageCount()));
  autosave();
}

void MainWindow::updatePagesTitle() {
  if (!m_pagesDock) return;
  if (m_project.pages.isEmpty() || m_project.workEnd <= 0)
    m_pagesDock->setWindowTitle("Pages");
  else
    m_pagesDock->setWindowTitle(
      QString("Pages (%1-%2)").arg(m_project.workStart).arg(m_project.workEnd));
}

void MainWindow::editDurations() {
  QVector<double> durs = NS::Timeline::markersToDurations(
    m_timeline->markers(), 60.0, qMax(1, m_project.pageCount()));
  PageDurationsDialog dlg(durs, this);
  dlg.exec();
  m_timeline->setDurations(durs, 60.0);
}

void MainWindow::onPageSelected(int row) {
  if (row < 0 || row >= m_project.pages.size() || row == m_current) return;
  showPage(row);
}

// Fit the whole page into the stage: full width AND full height visible,
// zoom multiplies the fitted size (scroll for the rest).
void MainWindow::fitViewer() {
  if (m_pagePixmap.isNull() || !m_stageScroll) return;
  QSize avail = m_stageScroll->viewport()->size() - QSize(4, 4);
  if (avail.width() < 50 || avail.height() < 50) return;
  QSize target(static_cast<int>(avail.width() * m_zoom),
               static_cast<int>(avail.height() * m_zoom));
  m_viewer->setPixmap(m_pagePixmap.scaled(
    target, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

bool MainWindow::eventFilter(QObject *obj, QEvent *ev) {
  if (m_stageScroll && obj == m_stageScroll->viewport()
      && ev->type() == QEvent::Resize)
    fitViewer();
  return QMainWindow::eventFilter(obj, ev);
}

// Lazy gallery thumbnails: one background task decodes small previews
// (full-res decode still happens per-page in showPage).
// Threading rules: QImage may cross threads (implicitly shared), but QPixmap
// and QListWidgetItem may only be touched on the GUI thread — decoding here,
// pixmap+icon assignment there. Paths are snapshotted so a re-import mid-run
// can't pull the vector out from under the worker.
void MainWindow::loadThumbnails() {
  QStringList paths;
  for (auto &pg : m_project.pages) paths << pg.imagePath;
  const int cap = qMin(1000, paths.size());
  if (cap <= 0) return;
  auto *task = new Task([this, paths, cap](auto progress, auto cancelled) {
    for (int i = 0; i < cap; ++i) {
      if (cancelled()) return;
      QImageReader rd(paths[i]);
      rd.setAutoTransform(true);
      QSize s = rd.size();
      if (s.isValid() && qMax(s.width(), s.height()) > 256)
        rd.setScaledSize(s * (256.0 / qMax(s.width(), s.height())));
      QImage img = rd.read();
      if (!img.isNull()) {
        QMetaObject::invokeMethod(m_pageList, [this, i, img]{
          if (auto *it = m_pageList->item(i)) it->setIcon(QIcon(QPixmap::fromImage(img)));
        }, Qt::QueuedConnection);
      }
      if (i % 25 == 0) progress(i * 100 / cap, "Loading thumbnails...");
    }
    progress(100, "Ready");
  });
  connect(task, &Task::progress, this, [this](int p, const QString &s){
    if (p < 100) setStatus(s, p); else setStatus("Ready");
  });
  QThreadPool::globalInstance()->start(task);
}

void MainWindow::zoomIn() {
  m_zoom = qMin(3.0, m_zoom + 0.15);
  m_settings.setPageZoom(m_zoom);
  if (m_zoomLabel) m_zoomLabel->setText(QString("%1%").arg(qRound(m_zoom * 100)));
  fitViewer();
}

void MainWindow::zoomOut() {
  m_zoom = qMax(0.3, m_zoom - 0.15);
  m_settings.setPageZoom(m_zoom);
  if (m_zoomLabel) m_zoomLabel->setText(QString("%1%").arg(qRound(m_zoom * 100)));
  fitViewer();
}

void MainWindow::showPage(int idx) {
  if (idx < 0 || idx >= m_project.pages.size()) return;
  m_current = idx;
  if (m_pageList && m_pageList->currentRow() != idx) {
    const QSignalBlocker block(m_pageList);
    m_pageList->setCurrentRow(idx);
  }
  if (m_zoomLabel) m_zoomLabel->setText(QString("%1%").arg(qRound(m_zoom * 100)));
  {
    const QSignalBlocker block(m_descEdit);
    m_descEdit->setPlainText(m_project.pages[idx].description);
  }
  const auto &pg = m_project.pages[idx];
  // Zero-copy decode: downscale at max_side=2048 in worker, pixmap only on GUI.
  QFuture<QImage> fut = QtConcurrent::run([path = pg.imagePath]{
    QImageReader rd(path);
    rd.setAutoTransform(true);
    QSize s = rd.size();
    if (s.isValid()) {
      int m = qMax(s.width(), s.height());
      if (m > 2048) rd.setScaledSize(s * (2048.0 / m));
    }
    return rd.read();
  });
  auto *watch = new QFutureWatcher<QImage>(this);
  connect(watch, &QFutureWatcher<QImage>::finished, this, [this, watch, idx]{
    Q_UNUSED(idx);
    QImage img = watch->result();
    if (!img.isNull()) {
      m_pagePixmap = QPixmap::fromImage(img);
      fitViewer();
    }
    watch->deleteLater();
  });
  watch->setFuture(fut);

  // Cast pills (checkable, green when present)
  QLayout *lo = m_castRow->layout();
  while (lo->count()) { delete lo->takeAt(0)->widget(); }
  m_castLabel->setText(QString("Cast - Page %1:").arg(pg.number));
  QStringList blocks;
  for (auto &b : pg.blocks)
    blocks << QString("%1: %2").arg(b.speaker.isEmpty() ? "Text" : b.speaker, b.content);
  m_blocksEdit->setPlainText(blocks.join("\n"));
  for (auto &c : m_project.characters) {
    auto *pill = new QPushButton(c.name, m_castRow);
    pill->setCheckable(true);
    pill->setChecked(pg.cast.contains(c.name));
    connect(pill, &QPushButton::toggled, this, [this, idx, name = c.name](bool on){
      auto &cast = m_project.pages[idx].cast;
      if (on && !cast.contains(name)) cast << name;
      if (!on) cast.removeAll(name);
    });
    lo->addWidget(pill);
  }
}

void MainWindow::onPageContextMenu(const QPoint &pos) {
  QListWidgetItem *hit = m_pageList->itemAt(pos);
  if (!hit) return;
  int row = m_pageList->row(hit);
  if (row < 0 || row >= m_project.pages.size()) return;
  const int pgNum = m_project.pages[row].number;
  const bool isExcluded = m_project.pages[row].excluded;
  QMenu menu(this);
  menu.addAction(isExcluded ? "Include in export" : "Exclude from export", this, [this, row]{
    m_project.pages[row].excluded = !m_project.pages[row].excluded;
    refreshPageList();
    if (m_current == row) showPage(row);
    autosave();
  });
  menu.addSeparator();
  if (!m_project.audioPath.isEmpty() && QFile::exists(m_project.audioPath)) {
    menu.addAction("Play page narration", this, [this, row, pgNum]{
      auto [from, to] = m_project.workRange();
      const QVector<double> &mk = m_project.markers;
      if (mk.isEmpty() && m_project.pages.size() > 1) return;
      const int pgIdx = row - from + 1;
      if (pgIdx < 0 || pgIdx > mk.size()) return;
      const double start = (pgIdx == 0) ? 0.0 : mk[pgIdx - 1];
      const double end = (pgIdx < mk.size()) ? mk[pgIdx] : wavSeconds(m_project.audioPath);
      const QString seg = QDir::current().absoluteFilePath(
        QString("temp/preview_page_%1.wav").arg(pgNum));
      QDir().mkpath("temp");
      QProcess proc;
      proc.start(VideoService::resolveFfmpeg(),
        {"-y", "-i", m_project.audioPath,
         "-ss", QString::number(start, 'f', 4),
         "-to", QString::number(end, 'f', 4),
         "-c", "copy", seg});
      proc.waitForFinished(10000);
      if (QFile::exists(seg)) {
        m_preview->stop();
        m_preview->setSource(QUrl::fromLocalFile(seg));
        m_preview->play();
        setStatus(QString("Playing page %1 narration (%2-%3s)").arg(pgNum)
                     .arg(start, 0, 'f', 2).arg(end, 0, 'f', 2));
      }
    });
    menu.addAction("Stop playback", this, [this]{
      m_preview->stop();
    });
  }
  menu.exec(m_pageList->mapToGlobal(pos));
}

void MainWindow::reviewNarrations() {
  auto [from, to] = m_project.workRange();
  if (m_project.audioPath.isEmpty() || !QFile::exists(m_project.audioPath)) {
    setStatus("No audio to review - synthesize first.");
    return;
  }
  // Auto-detect textless pages and show a review summary
  int textlessCount = 0;
  QStringList textlessPages;
  for (auto &pg : m_project.pages) {
    if (pg.number >= from && pg.number <= to && pg.blocks.isEmpty()) {
      textlessCount++;
      textlessPages << QString("Page %1").arg(pg.number);
    }
  }
  if (textlessCount == 0) {
    setStatus("No textless pages detected in work range.");
    return;
  }
  QMessageBox box(this);
  box.setWindowTitle("Narration Review");
  box.setIcon(QMessageBox::Information);
  box.setText(QString("Found %1 textless page(s) with potentially less accurate narrations:\n\n%2\n\n"
                      "Right-click pages in the gallery to exclude them from export, "
                      "or click 'Play page narration' to preview each one.")
               .arg(textlessCount).arg(textlessPages.join(", ")));
  box.setStandardButtons(QMessageBox::Ok);
  box.exec();
}

void MainWindow::refreshPageList() {
  m_pageList->clear();
  for (auto &pg : m_project.pages) {
    auto *item = new QListWidgetItem(
      QIcon(), QString("Pg %1").arg(pg.number), m_pageList);
    item->setData(Qt::UserRole, pg.imagePath);
    item->setTextAlignment(Qt::AlignHCenter);
    if (pg.excluded) {
      item->setForeground(QColor(128, 128, 128));
      item->setToolTip("Excluded from export");
    } else {
      item->setToolTip("");
    }
  }
  loadThumbnails();
}

void MainWindow::autosave() {
  m_project.narration = m_narrationEdit->toPlainText();
  QString err;
  QDir().mkpath("projects");
  // Reuse single autosave file (prevents unbounded rows).
  QFile::remove(m_dbPath + "-wal"); QFile::remove(m_dbPath + "-shm");
  QFile::remove(m_dbPath);
  NS::ProjectStore::save(m_dbPath, m_project, &err);
  m_settings.saveMainWindowState(saveState(), 1);
}
