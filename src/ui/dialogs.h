#pragma once
#include "core/project.h"
#include <QDialog>
#include <QStringList>

class QCheckBox;
class QSpinBox;
class MangaDxService;

// Characters / Synopsis / MangaDx / Webtoon / Export / PageDurations dialogs.
// Improvement: thumbs + preview + Popular/Latest tabs in MangaDx dialog.
class CharactersDialog : public QDialog {
  Q_OBJECT
public:
  explicit CharactersDialog(QVector<NS::Character> &chars, QWidget *parent = nullptr);
};

class SynopsisDialog : public QDialog {
  Q_OBJECT
public:
  explicit SynopsisDialog(QString &synopsis, QWidget *parent = nullptr);
};

class ExportDialog : public QDialog {
  Q_OBJECT
public:
  struct Result { int width = 1280, height = 720; QString bgMode = "black"; QString bgImage; };
  explicit ExportDialog(Result &r, QWidget *parent = nullptr);
};

class PageDurationsDialog : public QDialog {
  Q_OBJECT
public:
  explicit PageDurationsDialog(QVector<double> &durations, QWidget *parent = nullptr);
};

// Work range: 1-based inclusive pages OCR/Describe/Narrate/Voice act on.
// toPage <= 0 (or All checked) means "all pages", like Python's None range.
class WorkRangeDialog : public QDialog {
  Q_OBJECT
public:
  explicit WorkRangeDialog(int pageCount, int fromPage, int toPage,
                           QWidget *parent = nullptr);
  int fromPage() const;
  int toPage() const; // <= 0 = all pages
private:
  QSpinBox *m_from = nullptr, *m_to = nullptr;
  QCheckBox *m_all = nullptr;
};

// MangaDx browser: search with cover thumbs → chapter list → download pages.
// Network runs off the GUI thread; covers stream in as they arrive.
class MangaDxDialog : public QDialog {
  Q_OBJECT
public:
  struct Result { QString title; QStringList images; };
  explicit MangaDxDialog(MangaDxService *svc, Result &r, QWidget *parent = nullptr);
};

// Webtoon source: series URL/slug + chapter range (Python parity: limit the
// episodes instead of always pulling everything).
class WebtoonDialog : public QDialog {
  Q_OBJECT
public:
  struct Result {
    QString url;
    int startChapter = 1;   // first episode to fetch
    int endChapter = 0;     // 0 = up to the newest
    bool latestOnly = true; // newest episode only (ignores the range)
  };
  explicit WebtoonDialog(Result &r, QWidget *parent = nullptr);
};

class AppSettings;

// Kokoro voice picker: voice + speed + test + downloader for missing files.
class VoiceDialog : public QDialog {
  Q_OBJECT
public:
  explicit VoiceDialog(AppSettings &settings, QWidget *parent = nullptr);
};

// App settings mirroring the Python .env backends:
// OpenAI-compatible cloud API > Gemini > local Ollama.
class SettingsDialog : public QDialog {
  Q_OBJECT
public:
  explicit SettingsDialog(AppSettings &settings, QWidget *parent = nullptr);
};

// Big-text editor for a page's visual description (popup alternative to the
// inline stage field).
class DescriptionDialog : public QDialog {
  Q_OBJECT
public:
  explicit DescriptionDialog(int pageNumber, QString &description,
                             QWidget *parent = nullptr);
};
