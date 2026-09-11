#include "dialogs.h"
#include "core/settings.h"
#include "icons.h"
#include "services/kokoro_engine.h"
#include "services/kokoro_voices.h"
#include "services/mangadx_service.h"
#include "services/tts_service.h"
#include <QAudioOutput>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMediaPlayer>
#include <QNetworkReply>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStyle>
#include <QTabBar>
#include <QVBoxLayout>
#include <QtConcurrent>

CharactersDialog::CharactersDialog(QVector<NS::Character> &chars, QWidget *parent)
  : QDialog(parent) {
  setWindowTitle("Characters"); resize(420, 320);
  auto *lay = new QVBoxLayout(this);
  auto *list = new QListWidget(this);
  for (auto &c : chars) list->addItem(c.name + (c.role.isEmpty() ? "" : " (" + c.role + ")"));
  lay->addWidget(list);
  auto *row = new QHBoxLayout();
  auto *name = new QLineEdit(this); name->setPlaceholderText("Name (e.g. MC)");
  auto *role = new QLineEdit(this); role->setPlaceholderText("Role (optional)");
  auto *add = new QPushButton("Add", this);
  QObject::connect(add, &QPushButton::clicked, this, [&, name, role, list]{
    if (name->text().trimmed().isEmpty()) return;
    NS::Character c; c.name = name->text().trimmed(); c.role = role->text().trimmed();
    chars.append(c);
    list->addItem(c.name + (c.role.isEmpty() ? "" : " (" + c.role + ")"));
    name->clear(); role->clear();
  });
  row->addWidget(name); row->addWidget(role); row->addWidget(add);
  lay->addLayout(row);
  auto *btns = new QDialogButtonBox(QDialogButtonBox::Close, this);
  QObject::connect(btns, &QDialogButtonBox::rejected, this, &QDialog::accept);
  lay->addWidget(btns);
}

SynopsisDialog::SynopsisDialog(QString &synopsis, QWidget *parent) : QDialog(parent) {
  setWindowTitle("Synopsis - world premise"); resize(520, 340);
  auto *lay = new QVBoxLayout(this);
  auto *edit = new QPlainTextEdit(this);
  edit->setPlainText(synopsis);
  edit->setPlaceholderText("World bible / premise - injected at prompt top, used on textless pages...");
  lay->addWidget(edit);
  auto *btns = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
  QObject::connect(btns, &QDialogButtonBox::accepted, this, [&, edit]{ synopsis = edit->toPlainText(); accept(); });
  QObject::connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
  lay->addWidget(btns);
}

ExportDialog::ExportDialog(Result &r, QWidget *parent) : QDialog(parent) {
  setWindowTitle("Export video"); resize(380, 220);
  auto *form = new QFormLayout(this);
  auto *w = new QComboBox(this); w->addItems({"1280x720", "1920x1080", "1080x1920"});
  auto *bg = new QComboBox(this); bg->addItems({"black", "white", "blur", "image"});
  bg->setCurrentText(r.bgMode);
  form->addRow("Canvas", w); form->addRow("Background", bg);
  auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  QObject::connect(btns, &QDialogButtonBox::accepted, this, [&, w, bg]{
    const QStringList wh = w->currentText().split("x");
    r.width = wh[0].toInt(); r.height = wh[1].toInt(); r.bgMode = bg->currentText(); accept();
  });
  QObject::connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
  form->addWidget(btns);
}

PageDurationsDialog::PageDurationsDialog(QVector<double> &durations, QWidget *parent)
  : QDialog(parent) {
  setWindowTitle("Page durations"); resize(320, 400);
  auto *lay = new QVBoxLayout(this);
  auto *list = new QListWidget(this);
  for (int i = 0; i < durations.size(); ++i)
    list->addItem(QString("Page %1 - %2s").arg(i + 1).arg(durations[i], 0, 'f', 2));
  lay->addWidget(list);
  auto *spin = new QDoubleSpinBox(this);
  spin->setRange(0.5, 120.0); spin->setValue(5.0); spin->setSuffix(" s");
  auto *apply = new QPushButton("Set selected", this);
  QObject::connect(apply, &QPushButton::clicked, this, [&, list, spin]{
    for (auto *it : list->selectedItems()) {
      int row = list->row(it);
      durations[row] = spin->value();
      it->setText(QString("Page %1 - %2s").arg(row + 1).arg(durations[row], 0, 'f', 2));
    }
  });
  lay->addWidget(spin); lay->addWidget(apply);
  auto *btns = new QDialogButtonBox(QDialogButtonBox::Close, this);
  QObject::connect(btns, &QDialogButtonBox::rejected, this, &QDialog::accept);
  lay->addWidget(btns);
}

WorkRangeDialog::WorkRangeDialog(int pageCount, int fromPage, int toPage,
                                 QWidget *parent)
  : QDialog(parent) {
  setWindowTitle("Work Range");
  resize(320, 200);
  auto *lay = new QVBoxLayout(this);
  auto *hint = new QLabel(
    QString("OCR, Describe, Narrate and Voice act on these pages (1-%1).")
    .arg(qMax(1, pageCount)), this);
  hint->setObjectName("muted");
  hint->setWordWrap(true);
  lay->addWidget(hint);
  auto *form = new QFormLayout();
  m_from = new QSpinBox(this);
  m_from->setRange(1, qMax(1, pageCount));
  m_from->setValue(qBound(1, fromPage, qMax(1, pageCount)));
  m_to = new QSpinBox(this);
  m_to->setRange(1, qMax(1, pageCount));
  m_to->setValue(toPage > 0 ? qBound(1, toPage, qMax(1, pageCount))
                            : qMax(1, pageCount));
  form->addRow("From page", m_from);
  form->addRow("To page", m_to);
  lay->addLayout(form);
  m_all = new QCheckBox("All pages", this);
  m_all->setChecked(toPage <= 0);
  QObject::connect(m_all, &QCheckBox::toggled, this, [this](bool on){
    m_from->setEnabled(!on);
    m_to->setEnabled(!on);
  });
  m_from->setEnabled(toPage > 0);
  m_to->setEnabled(toPage > 0);
  lay->addWidget(m_all);
  lay->addStretch(1);
  auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  QObject::connect(btns, &QDialogButtonBox::accepted, this, &QDialog::accept);
  QObject::connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
  lay->addWidget(btns);
}

int WorkRangeDialog::fromPage() const { return m_from->value(); }
int WorkRangeDialog::toPage() const { return m_all->isChecked() ? 0 : m_to->value(); }

// ---- MangaDx browser ----

MangaDxDialog::MangaDxDialog(MangaDxService *svc, Result &r, QWidget *parent)
  : QDialog(parent) {
  setWindowTitle("Import from MangaDx");
  resize(760, 520);

  auto *lay = new QVBoxLayout(this);
  auto *tabs = new QTabBar(this);
  tabs->addTab("Popular");
  tabs->addTab("Latest");
  tabs->addTab("Search");
  tabs->setExpanding(false);
  lay->addWidget(tabs);
  auto *searchRow = new QHBoxLayout();
  auto *query = new QLineEdit(this);
  query->setPlaceholderText("Search manga title...");
  query->setClearButtonEnabled(true);
  auto *go = new QPushButton("Search", this);
  go->setIcon(Icons::get("search"));
  go->setDefault(true);
  searchRow->addWidget(query, 1);
  searchRow->addWidget(go);
  lay->addLayout(searchRow);

  auto *split = new QSplitter(Qt::Horizontal, this);
  auto *results = new QListWidget(split);
  results->setIconSize(QSize(64, 90));
  results->setMinimumWidth(280);
  auto *chapters = new QListWidget(split);
  lay->addWidget(split, 1);

  auto *status = new QLabel("Type a title and press Search.", this);
  status->setObjectName("muted");
  status->setWordWrap(true);
  lay->addWidget(status);

  auto *dlRow = new QHBoxLayout();
  auto *dl = new QPushButton("Download chapter", this);
  dl->setIcon(Icons::get("download"));
  dl->setEnabled(false);
  auto *cancel = new QPushButton("Close", this);
  cancel->setObjectName("ghost");
  dlRow->addStretch(1);
  dlRow->addWidget(cancel);
  dlRow->addWidget(dl);
  lay->addLayout(dlRow);

  auto *nam = new QNetworkAccessManager(this);

  struct State {
    QVector<MangaDxService::Manga> mangas;
    QVector<MangaDxService::Chapter> chs;
    int mangaRow = -1;
  };
  auto *st = new State();
  // parent State to dialog lifetime via QObject cleanup
  QObject::connect(this, &QObject::destroyed, [st]{ delete st; });

  auto fetchCovers = [=]{
    for (int i = 0; i < st->mangas.size(); ++i) {
      const QString &url = st->mangas[i].coverUrl;
      if (url.isEmpty()) continue;
      QNetworkReply *rep = nam->get(QNetworkRequest(QUrl(url)));
      rep->setProperty("row", i);
      QObject::connect(rep, &QNetworkReply::finished, this, [=]{
        rep->deleteLater();
        if (rep->error() != QNetworkReply::NoError) return;
        QPixmap pm;
        if (!pm.loadFromData(rep->readAll())) return;
        if (auto *it = results->item(rep->property("row").toInt()))
          it->setIcon(QIcon(pm));
      });
    }
  };

  // Shared browse/search fill: errors surface with reasons (DNS blocks show
  // as "No results" otherwise, hiding the real cause).
  auto showMangas = [=](const QVector<MangaDxService::Manga> &list,
                        const QString &err, const QString &what) {
    st->mangas = list;
    st->mangaRow = -1;
    go->setEnabled(true);
    if (!err.isEmpty()) { status->setText(what + " failed: " + err); return; }
    if (st->mangas.isEmpty()) {
      status->setText(what + " returned nothing. Try another title.");
      return;
    }
    for (auto &m : st->mangas)
      results->addItem(m.title.isEmpty() ? m.id : m.title);
    status->setText(QString("%1 titles - pick one to list chapters.").arg(st->mangas.size()));
    fetchCovers();
  };

  auto loadBrowse = [=](int mode) { // 0 popular, 1 latest
    go->setEnabled(false);
    status->setText(mode == 0 ? "Loading popular manga..." : "Loading latest manga...");
    results->clear(); chapters->clear(); dl->setEnabled(false);
    auto *watch = new QFutureWatcher<QPair<QVector<MangaDxService::Manga>,QString>>(this);
    QObject::connect(watch, &QFutureWatcherBase::finished, this, [=]{
      auto [list, err] = watch->result();
      watch->deleteLater();
      showMangas(list, err, mode == 0 ? "Popular" : "Latest");
    });
    watch->setFuture(QtConcurrent::run([svc, mode]() -> QPair<QVector<MangaDxService::Manga>,QString> {
      QString err;
      auto out = mode == 0 ? svc->popular(20, &err) : svc->latest(20, &err);
      return {out, err};
    }));
  };

  QObject::connect(tabs, &QTabBar::currentChanged, this, [=](int i){
    // Search tab keeps the query row; browse tabs load immediately.
    for (int k = 0; k < searchRow->count(); ++k) {
      if (QWidget *w = searchRow->itemAt(k)->widget()) w->setVisible(i == 2);
    }
    if (i < 2) loadBrowse(i);
  });

  QObject::connect(go, &QPushButton::clicked, this, [=]{
    const QString q = query->text().trimmed();
    if (q.isEmpty()) return;
    go->setEnabled(false);
    status->setText("Searching MangaDx...");
    results->clear(); chapters->clear(); dl->setEnabled(false);
    auto *watch = new QFutureWatcher<QPair<QVector<MangaDxService::Manga>,QString>>(this);
    QObject::connect(watch, &QFutureWatcherBase::finished, this, [=]{
      auto [list, err] = watch->result();
      watch->deleteLater();
      showMangas(list, err, "Search");
    });
    watch->setFuture(QtConcurrent::run([svc, q]() -> QPair<QVector<MangaDxService::Manga>,QString> {
      QString err;
      auto out = svc->search(q, 20, &err);
      return {out, err};
    }));
  });

  QObject::connect(results, &QListWidget::currentRowChanged, this, [=](int row){
    if (row < 0 || row >= st->mangas.size()) return;
    st->mangaRow = row;
    chapters->clear(); dl->setEnabled(false);
    status->setText("Loading chapters...");
    const QString id = st->mangas[row].id;
    auto *watch = new QFutureWatcher<QPair<QVector<MangaDxService::Chapter>,QString>>(this);
    QObject::connect(watch, &QFutureWatcherBase::finished, this, [=]{
      auto [chs, err] = watch->result();
      st->chs = chs;
      watch->deleteLater();
      if (!err.isEmpty()) { status->setText("Chapters failed: " + err); return; }
      if (st->chs.isEmpty()) { status->setText("No English chapters found."); return; }
      for (auto &c : st->chs) {
        QString label = c.chapter.isEmpty() ? c.title : ("Ch " + c.chapter);
        if (!c.title.isEmpty() && !c.chapter.isEmpty()) label += " - " + c.title;
        chapters->addItem(label);
      }
      status->setText(QString("%1 chapters - pick one, then Download.").arg(st->chs.size()));
    });
    watch->setFuture(QtConcurrent::run([svc, id]() -> QPair<QVector<MangaDxService::Chapter>,QString> {
      QString err;
      return {svc->chapters(id, &err), err};
    }));
  });

  QObject::connect(chapters, &QListWidget::currentRowChanged, this, [=](int row){
    dl->setEnabled(row >= 0 && row < st->chs.size());
  });

  QObject::connect(dl, &QPushButton::clicked, this, [=, &r]{
    const int cr = chapters->currentRow();
    if (st->mangaRow < 0 || cr < 0) return;
    dl->setEnabled(false);
    status->setText("Downloading pages (bulk HTTP/2)...");
    const QString mangaId = st->mangas[st->mangaRow].id;
    const QString title = st->mangas[st->mangaRow].title;
    const QString chId = st->chs[cr].id;
    auto *watch = new QFutureWatcher<QPair<QStringList,QString>>(this);
    QObject::connect(watch, &QFutureWatcherBase::finished, this, [=, &r]{
      auto [imgs, err] = watch->result();
      watch->deleteLater();
      if (imgs.isEmpty()) {
        status->setText(err.isEmpty() ? "Download failed - check connection and retry."
                                      : "Download failed: " + err);
        dl->setEnabled(true);
        return;
      }
      r.title = title;
      r.images = imgs;
      accept();
    });
    watch->setFuture(QtConcurrent::run([svc, mangaId, chId, title]() -> QPair<QStringList,QString> {
      QString err;
      return {svc->downloadChapter(mangaId, chId, title, &err), err};
    }));
  });

  QObject::connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
  QObject::connect(query, &QLineEdit::returnPressed, go, &QPushButton::click);
  // Landing: Popular list with covers, like the Python dialog.
  for (int k = 0; k < searchRow->count(); ++k)
    if (QWidget *w = searchRow->itemAt(k)->widget()) w->hide();
  loadBrowse(0);
}

WebtoonDialog::WebtoonDialog(Result &r, QWidget *parent) : QDialog(parent) {
  setWindowTitle("Import from Webtoon");
  resize(480, 260);
  auto *lay = new QVBoxLayout(this);
  auto *hint = new QLabel("Paste the series URL or slug, then choose how many episodes to fetch.", this);
  hint->setObjectName("muted");
  hint->setWordWrap(true);
  lay->addWidget(hint);
  auto *edit = new QLineEdit(this);
  edit->setPlaceholderText("https://www.webtoons.com/...  or  series-slug");
  edit->setClearButtonEnabled(true);
  if (!r.url.isEmpty()) edit->setText(r.url);
  lay->addWidget(edit);
  auto *latest = new QCheckBox("Latest episode only", this);
  latest->setChecked(r.latestOnly);
  lay->addWidget(latest);
  auto *form = new QFormLayout();
  auto *start = new QSpinBox(this);
  start->setRange(1, 100000);
  start->setValue(qMax(1, r.startChapter));
  start->setEnabled(!r.latestOnly);
  auto *end = new QSpinBox(this);
  end->setRange(0, 100000);
  end->setSpecialValueText("newest");
  end->setValue(qMax(0, r.endChapter));
  end->setEnabled(!r.latestOnly);
  form->addRow("Start episode", start);
  form->addRow("End episode", end);
  lay->addLayout(form);
  QObject::connect(latest, &QCheckBox::toggled, this, [=](bool on){
    start->setEnabled(!on);
    end->setEnabled(!on);
  });
  auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  QObject::connect(btns, &QDialogButtonBox::accepted, this, [&, edit, latest, start, end]{
    if (edit->text().trimmed().isEmpty()) return;
    r.url = edit->text().trimmed();
    r.latestOnly = latest->isChecked();
    r.startChapter = start->value();
    r.endChapter = end->value();
    accept();
  });
  QObject::connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
  lay->addWidget(btns);
}

// ---- Kokoro voice picker ----

VoiceDialog::VoiceDialog(AppSettings &settings, QWidget *parent) : QDialog(parent) {
  setWindowTitle("Narration voice - Kokoro");
  resize(460, 340);
  auto *lay = new QVBoxLayout(this);

  auto *status = new QLabel(this);
  status->setObjectName("muted");
  status->setWordWrap(true);
  lay->addWidget(status);

  auto refreshStatus = [=]{
    QString reason;
    if (KokoroTtsEngine::available(&reason)) {
      QString err;
      const int n = KokoroTtsEngine::voices(&err).size();
      status->setText(QString("Kokoro ready - %1 voices, 24kHz.").arg(n));
    } else {
      status->setText(reason + " (~220MB: ONNX Runtime + int8 model + voices + espeak-ng).");
    }
  };
  refreshStatus();

  auto *form = new QFormLayout();
  auto *voice = new QComboBox(this);
  {
    QString err;
    for (const QString &id : KokoroTtsEngine::voices(&err))
      voice->addItem(kokoroVoiceDisplay(id), id);
    const int at = voice->findData(settings.speechVoice());
    voice->setCurrentIndex(at >= 0 ? at : qMax(0, voice->findData("af_heart")));
  }
  auto *speedRow = new QHBoxLayout();
  auto *speed = new QSlider(Qt::Horizontal, this);
  speed->setRange(50, 200);
  speed->setValue(qRound(settings.speechSpeed() * 100));
  auto *speedLabel = new QLabel(QString("%1%").arg(speed->value()), this);
  speedLabel->setFixedWidth(48);
  QObject::connect(speed, &QSlider::valueChanged, this, [=](int v){
    speedLabel->setText(QString("%1%").arg(v));
  });
  speedRow->addWidget(speed, 1);
  speedRow->addWidget(speedLabel);
  form->addRow("Voice", voice);
  form->addRow("Speed", speedRow);
  lay->addLayout(form);

  auto *player = new QMediaPlayer(this);
  player->setAudioOutput(new QAudioOutput(this));

  auto *testRow = new QHBoxLayout();
  auto *test = new QPushButton("Test voice", this);
  test->setIcon(Icons::get("play"));
  auto *dl = new QPushButton("Download voice files", this);
  dl->setIcon(Icons::get("download"));
  testRow->addWidget(test);
  testRow->addWidget(dl);
  testRow->addStretch(1);
  lay->addLayout(testRow);
  lay->addStretch(1);

  QObject::connect(test, &QPushButton::clicked, this, [=]{
    test->setEnabled(false);
    status->setText("Rendering sample...");
    const QString v = voice->currentData().toString();
    const double sp = speed->value() / 100.0;
    auto *watch = new QFutureWatcher<bool>(this);
    QObject::connect(watch, &QFutureWatcherBase::finished, this, [=]{
      const bool ok = watch->result();
      watch->deleteLater();
      test->setEnabled(true);
      if (!ok) { status->setText("Sample failed - see status bar."); refreshStatus(); return; }
      player->setSource(QUrl::fromLocalFile(
        QDir::current().absoluteFilePath("temp/preview/kokoro_test.wav")));
      player->play();
      refreshStatus();
    });
    watch->setFuture(QtConcurrent::run([v, sp]{
      QDir().mkpath("temp/preview");
      KokoroTtsEngine eng(v, sp);
      QString err;
      return eng.synthesize(
        QString("Hello! I am %1, your narrator.").arg(v),
        QDir::current().absoluteFilePath("temp/preview/kokoro_test.wav"),
        nullptr, &err);
    }));
  });

  QObject::connect(dl, &QPushButton::clicked, this, [=]{
    dl->setEnabled(false);
    status->setText("Downloading voice files - watch the status bar, this takes a few minutes...");
    QProcess *proc = new QProcess(this);
    QObject::connect(proc, &QProcess::finished, this, [=](int code, int){
      proc->deleteLater();
      dl->setEnabled(true);
      status->setText(code == 0
        ? "Download finished - voices ready."
        : "Download failed. Run scripts/fetch-kokoro.ps1 manually in PowerShell.");
      refreshStatus();
    });
    proc->start("powershell",
      {"-ExecutionPolicy", "Bypass", "-File", "scripts/fetch-kokoro.ps1"});
    if (!proc->waitForStarted(5000)) {
      status->setText("Could not start PowerShell.");
      dl->setEnabled(true);
      proc->deleteLater();
    }
  });

  auto *btns = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
  QObject::connect(btns, &QDialogButtonBox::accepted, this, [&, voice, speed]{
    settings.setSpeechVoice(voice->currentData().toString());
    settings.setSpeechSpeed(speed->value() / 100.0);
    accept();
  });
  QObject::connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
  lay->addWidget(btns);
}

// ---- App settings (AI backends, same priority as Python .env) ----

SettingsDialog::SettingsDialog(AppSettings &settings, QWidget *parent)
  : QDialog(parent) {
  setWindowTitle("Settings - AI narration");
  resize(520, 420);
  auto *lay = new QVBoxLayout(this);

  auto *hint = new QLabel(
    "Backend priority: cloud API first, then Gemini, then local Ollama. "
    "Keys stay on this machine.", this);
  hint->setObjectName("muted");
  hint->setWordWrap(true);
  lay->addWidget(hint);

  auto textRow = [&](const QString &label, const QString &value,
                     const QString &placeholder) {
    auto *row = new QHBoxLayout();
    auto *lab = new QLabel(label, this);
    lab->setFixedWidth(92);
    auto *edit = new QLineEdit(this);
    edit->setText(value);
    edit->setPlaceholderText(placeholder);
    edit->setClearButtonEnabled(true);
    row->addWidget(lab);
    row->addWidget(edit, 1);
    lay->addLayout(row);
    return edit;
  };
  auto groupLabel = [&](const QString &t) {
    auto *l = new QLabel(t, this);
    QFont f = l->font(); f.setBold(true); l->setFont(f);
    lay->addWidget(l);
  };

  groupLabel("Cloud API (OpenAI-compatible: Groq, OpenRouter, ...)");
  auto *llmKeys = new QPlainTextEdit(this);
  llmKeys->setPlainText(settings.llmKeys().join("\n"));
  llmKeys->setPlaceholderText("API keys, one per line - auto-rotates on quota (blank to skip)");
  llmKeys->setFixedHeight(56);
  lay->addWidget(new QLabel("Keys", this));
  lay->addWidget(llmKeys);
  QLineEdit *llmBase = textRow("Base URL", settings.llmBase(), "https://api.groq.com/openai/v1");
  QLineEdit *llmModel = textRow("Model", settings.llmModel(), "llama-3.1-8b-instant");

  groupLabel("Gemini (Google AI Studio)");
  auto *gemKeys = new QPlainTextEdit(this);
  gemKeys->setPlainText(settings.geminiKeys().join("\n"));
  gemKeys->setPlaceholderText("API keys, one per line - auto-rotates on quota (blank to skip)");
  gemKeys->setFixedHeight(56);
  lay->addWidget(new QLabel("Keys", this));
  lay->addWidget(gemKeys);
  QLineEdit *gemModel = textRow("Model", settings.geminiModel(), "gemini-3.6-flash");
  QLineEdit *gemFall = textRow("Fallbacks", settings.geminiFallbacks(), "comma-separated models");

  groupLabel("Describe API (Gemini Vision - scene descriptions)");
  auto *descKeys = new QPlainTextEdit(this);
  descKeys->setPlainText(settings.describeKeys().join("\n"));
  descKeys->setPlaceholderText("API keys, one per line - auto-rotates on quota (blank to use Florence-2 local)");
  descKeys->setFixedHeight(56);
  lay->addWidget(new QLabel("Keys", this));
  lay->addWidget(descKeys);
  QLineEdit *descModel = textRow("Model", settings.describeModel(), "gemini-3.5-flash-lite");
  QLineEdit *descFall = textRow("Fallbacks", settings.describeFallbacks(), "comma-separated models");

  groupLabel("Ollama (local, offline)");
  QLineEdit *olUrl = textRow("URL", settings.ollamaUrl(), "http://localhost:11434");
  QLineEdit *olModel = textRow("Model", settings.ollamaModel(), "llama3.2");

  auto splitKeys = [](const QString &s) {
    QStringList out;
    for (const QString &t : s.split(QRegularExpression("[\n,;]+"), Qt::SkipEmptyParts)) {
      const QString k = t.trimmed();
      if (!k.isEmpty() && !out.contains(k)) out << k;
    }
    return out;
  };

  // Connection test: tiny prompt through the configured backend chain.
  auto *testRow = new QHBoxLayout();
  auto *test = new QPushButton("Test connection", this);
  test->setIcon(Icons::get("play"));
  auto *testStatus = new QLabel(this);
  testStatus->setObjectName("muted");
  testStatus->setWordWrap(true);
  testRow->addWidget(test);
  testRow->addWidget(testStatus, 1);
  lay->addLayout(testRow);
  QObject::connect(test, &QPushButton::clicked, this, [=, &settings]{
    test->setEnabled(false);
    testStatus->setText("Asking the model to say OK...");
    // snapshot fields (dialog may hold unsaved edits); first key of the pool
    const QStringList lks = splitKeys(llmKeys->toPlainText());
    const QStringList gks = splitKeys(gemKeys->toPlainText());
    const QString lk = lks.value(0), lb = llmBase->text().trimmed(),
                  lm = llmModel->text().trimmed(), gk = gks.value(0),
                  gm = gemModel->text().trimmed(), ou = olUrl->text().trimmed(),
                  om = olModel->text().trimmed();
    auto *watch = new QFutureWatcher<QString>(this);
    QObject::connect(watch, &QFutureWatcherBase::finished, this, [=]{
      const QString out = watch->result();
      watch->deleteLater();
      test->setEnabled(true);
      testStatus->setText(out.left(300));
    });
    watch->setFuture(QtConcurrent::run([lk, lb, lm, gk, gm, ou, om]{
      QString err, raw;
      if (!lk.isEmpty()) {
        OpenAiCompatNarrator nar(lb.isEmpty() ? "https://api.groq.com/openai/v1" : lb,
                                 lk, lm.isEmpty() ? "llama-3.1-8b-instant" : lm);
        raw = nar.narrate("Reply with exactly: OK", &err);
      } else if (!gk.isEmpty()) {
        OpenAiCompatNarrator nar("https://generativelanguage.googleapis.com/v1beta/openai",
                                 gk, gm.isEmpty() ? "gemini-3.6-flash" : gm);
        raw = nar.narrate("Reply with exactly: OK", &err);
      } else {
        OllamaNarrator nar(ou.isEmpty() ? "http://localhost:11434" : ou,
                           om.isEmpty() ? "llama3.2" : om);
        raw = nar.narrate("Reply with exactly: OK", &err);
      }
      if (!err.isEmpty() && raw.trimmed().isEmpty()) return QString("FAILED: " + err);
      return QString("Model replied: ") + raw.trimmed().left(200);
    }));
  });
  lay->addStretch(1);

  auto *btns = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
  QObject::connect(btns, &QDialogButtonBox::accepted, this,
                   [&, llmKeys, llmBase, llmModel, gemKeys, gemModel, gemFall, descKeys, descModel, descFall, olUrl, olModel]{
    auto keysOf = [](QPlainTextEdit *e) {
      QStringList out;
      for (const QString &t : e->toPlainText().split(QRegularExpression("[\n,;]+"), Qt::SkipEmptyParts)) {
        const QString k = t.trimmed();
        if (!k.isEmpty() && !out.contains(k)) out << k;
      }
      return out;
    };
    settings.setLlmKeys(keysOf(llmKeys));
    settings.setLlmBase(llmBase->text().trimmed());
    settings.setLlmModel(llmModel->text().trimmed());
    settings.setGeminiKeys(keysOf(gemKeys));
    settings.setGeminiModel(gemModel->text().trimmed());
    settings.setGeminiFallbacks(gemFall->text().trimmed());
    settings.setDescribeKeys(keysOf(descKeys));
    settings.setDescribeModel(descModel->text().trimmed());
    settings.setDescribeFallbacks(descFall->text().trimmed());
    settings.setOllamaUrl(olUrl->text().trimmed());
    settings.setOllamaModel(olModel->text().trimmed());
    accept();
  });
  QObject::connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
  lay->addWidget(btns);
}

// ---- Page description popup editor ----

DescriptionDialog::DescriptionDialog(int pageNumber, QString &description,
                                     QWidget *parent)
  : QDialog(parent) {
  setWindowTitle(QString("Describe page %1").arg(pageNumber));
  resize(560, 380);
  auto *lay = new QVBoxLayout(this);
  auto *hint = new QLabel(
    "What happens visually on this page. Narration is built from this on "
    "textless panels, so be concrete: who, where, action.", this);
  hint->setObjectName("muted");
  hint->setWordWrap(true);
  lay->addWidget(hint);
  auto *edit = new QPlainTextEdit(this);
  edit->setPlainText(description);
  edit->setPlaceholderText("A quiet street at dusk. The MC walks alone...");
  lay->addWidget(edit, 1);
  auto *btns = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
  QObject::connect(btns, &QDialogButtonBox::accepted, this, [&, edit]{
    description = edit->toPlainText();
    accept();
  });
  QObject::connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
  lay->addWidget(btns);
}
