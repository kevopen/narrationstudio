#pragma once
#include <QRect>
#include <QString>
#include <QStringList>
#include <QVector>

// In-memory project model (SQLite is the source of truth on disk).
namespace NS {

struct TextBlock { QString content; QString type = "dialogue"; QString speaker; QRect rect; };
struct Page {
  int number = 0; QString imagePath; QString description;
  QVector<TextBlock> blocks; QStringList cast;
};
struct Character { qlonglong id = 0; QString name; QString role; QString voiceStyle; };

// Char range of the narration script belonging to one page (from the
// [PAGE n] parse at Narrate time). Used to slice per-page audio.
struct PageSeg { int page = 0; int start = 0; int end = 0; };

struct Project {
  QString name = "Untitled";
  QString synopsis;
  int workStart = 1, workEnd = 0;           // 0 = all pages
  QVector<Character> characters;
  QVector<Page> pages;
  QString narration;
  QVector<PageSeg> segments;   // narration char ranges per page (may be stale)
  QVector<double> markers;                  // page-boundary seconds
  QString audioPath;
  int pageCount() const { return pages.size(); }
  QPair<int,int> workRange() const {
    int end = workEnd <= 0 ? pages.size() : qMin(workEnd, pages.size());
    return {qMax(1, workStart), qMax(end, 1)};
  }
};

} // namespace NS
