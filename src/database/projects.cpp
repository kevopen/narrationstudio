#include "projects.h"
#include "database/database.h"
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace NS {

// Qt binds a null QString as SQL NULL, which trips NOT NULL columns the
// moment a fresh import (null description, synopsis, ...) is saved.
// Normalize every text bind: null → empty string.
inline QString nn(const QString &s) { return s.isNull() ? QString("") : s; }

// One-shot DB handle: closes + removes the connection on scope exit so
// repeated saves/autosaves never pile up open handles on the same file
// (on Windows those stale handles make later rewrites fail).
struct ScopedDb {
  QSqlDatabase db;
  bool ok = false;
  explicit ScopedDb(const QString &path, QString *error) {
    ok = Database::open(path, db, error);
  }
  ~ScopedDb() {
    if (!db.isValid()) return;
    const QString name = db.connectionName();
    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(name);
  }
};

bool ProjectStore::save(const QString &dbPath, const Project &p, QString *error) {
  ScopedDb gdb(dbPath, error);
  if (!gdb.ok) return false;
  QSqlDatabase &db = gdb.db;
  if (!db.transaction()) { if (error) *error = "transaction failed"; return false; }
  QSqlQuery q(db);
  auto fail = [&](const QString &why) {
    if (error) *error = why + ": " + q.lastError().text();
    db.rollback(); return false;
  };
  // Single-project-per-file snapshot: wipe stale rows first, otherwise every
  // save duplicates thousands of pages (10k-page projects bloat fast).
  for (const char *t : {"page_characters", "text_blocks", "narration_segments",
                        "audio_markers", "timeline", "panels", "pages",
                        "characters", "projects"}) {
    q.prepare(QString("DELETE FROM %1").arg(t));
    if (!q.exec()) return fail(QString("cleanup ") + t);
  }
  q.prepare("INSERT INTO projects (name, narration, audio_path, synopsis, work_start, work_end) VALUES (?,?,?,?,?,?)");
  q.addBindValue(nn(p.name)); q.addBindValue(nn(p.narration)); q.addBindValue(nn(p.audioPath));
  q.addBindValue(nn(p.synopsis));
  q.addBindValue(p.workStart); q.addBindValue(p.workEnd);
  if (!q.exec()) return fail("insert project");
  qlonglong pid = q.lastInsertId().toLongLong();

  QMap<QString, qlonglong> charIds;
  for (const auto &c : p.characters) {
    q.prepare("INSERT INTO characters (project_id, name, role, voice_style) VALUES (?,?,?,?)");
    q.addBindValue(pid); q.addBindValue(nn(c.name)); q.addBindValue(nn(c.role)); q.addBindValue(nn(c.voiceStyle));
    if (!q.exec()) return fail("insert character");
    charIds[c.name] = q.lastInsertId().toLongLong();
  }
  for (const auto &pg : p.pages) {
    q.prepare("INSERT INTO pages (project_id, image_path, page_number, description, excluded) VALUES (?,?,?,?,?)");
    q.addBindValue(pid); q.addBindValue(nn(pg.imagePath)); q.addBindValue(pg.number); q.addBindValue(nn(pg.description)); q.addBindValue(pg.excluded ? 1 : 0);
    if (!q.exec()) return fail("insert page");
    qlonglong pageId = q.lastInsertId().toLongLong();
    for (const auto &b : pg.blocks) {
      q.prepare("INSERT INTO text_blocks (page_id, content, type) VALUES (?,?,?)");
      q.addBindValue(pageId); q.addBindValue(nn(b.content)); q.addBindValue(nn(b.type));
      if (!q.exec()) return fail("insert block");
    }
    for (const auto &name : pg.cast) {
      auto it = charIds.find(name);
      if (it == charIds.end()) continue;
      q.prepare("INSERT OR IGNORE INTO page_characters (page_id, character_id) VALUES (?,?)");
      q.addBindValue(pageId); q.addBindValue(it.value());
      if (!q.exec()) return fail("insert page_characters");
    }
  }
  for (const auto &s : p.segments) {
    q.prepare("INSERT INTO narration_segments (project_id, page_number, char_start, char_end) VALUES (?,?,?,?)");
    q.addBindValue(pid); q.addBindValue(s.page); q.addBindValue(s.start); q.addBindValue(s.end);
    if (!q.exec()) return fail("insert narration_segments");
  }
  for (int i = 0; i < p.markers.size(); ++i) {
    q.prepare("INSERT INTO audio_markers (project_id, idx, seconds) VALUES (?,?,?)");
    q.addBindValue(pid); q.addBindValue(i); q.addBindValue(p.markers[i]);
    if (!q.exec()) return fail("insert audio_markers");
  }
  if (!db.commit()) { if (error) *error = "commit failed"; return false; }
  return true;
}

bool ProjectStore::load(const QString &dbPath, Project &p, QString *error) {
  ScopedDb gdb(dbPath, error);
  if (!gdb.ok) return false;
  QSqlDatabase &db = gdb.db;
  QSqlQuery q(db);
  q.exec("SELECT id, name, narration, audio_path, synopsis, work_start, work_end FROM projects ORDER BY id DESC LIMIT 1");
  if (!q.next()) { if (error) *error = "no project"; return false; }
  qlonglong pid = q.value(0).toLongLong();
  p.name = q.value(1).toString(); p.narration = q.value(2).toString();
  p.audioPath = q.value(3).toString(); p.synopsis = q.value(4).toString();
  p.workStart = q.value(5).toInt(); p.workEnd = q.value(6).toInt();

  QMap<qlonglong, QString> idToName;
  q.prepare("SELECT id, name, role, voice_style FROM characters WHERE project_id=?");
  q.addBindValue(pid); q.exec();
  while (q.next()) {
    Character c; c.id = q.value(0).toLongLong();
    c.name = q.value(1).toString(); c.role = q.value(2).toString(); c.voiceStyle = q.value(3).toString();
    idToName[c.id] = c.name; p.characters.append(c);
  }
  q.prepare("SELECT id, image_path, page_number, description, excluded FROM pages WHERE project_id=? ORDER BY page_number");
  q.addBindValue(pid); q.exec();
  while (q.next()) {
    Page pg; qlonglong pageId = q.value(0).toLongLong();
    pg.imagePath = q.value(1).toString(); pg.number = q.value(2).toInt(); pg.description = q.value(3).toString();
    pg.excluded = q.value(4).toBool();
    QSqlQuery b(db);
    b.prepare("SELECT content, type FROM text_blocks WHERE page_id=?");
    b.addBindValue(pageId); b.exec();
    while (b.next()) { TextBlock t; t.content = b.value(0).toString(); t.type = b.value(1).toString(); pg.blocks.append(t); }
    QSqlQuery c(db);
    c.prepare("SELECT c.name FROM page_characters pc JOIN characters c ON c.id=pc.character_id WHERE pc.page_id=?");
    c.addBindValue(pageId); c.exec();
    while (c.next()) pg.cast << c.value(0).toString();
    p.pages.append(pg);
  }
  QSqlQuery s(db);
  s.prepare("SELECT page_number, char_start, char_end FROM narration_segments WHERE project_id=? ORDER BY page_number");
  s.addBindValue(pid); s.exec();
  while (s.next()) {
    NS::PageSeg seg;
    seg.page = s.value(0).toInt(); seg.start = s.value(1).toInt(); seg.end = s.value(2).toInt();
    p.segments.append(seg);
  }
  QSqlQuery am(db);
  am.prepare("SELECT seconds FROM audio_markers WHERE project_id=? ORDER BY idx");
  am.addBindValue(pid); am.exec();
  while (am.next()) p.markers.append(am.value(0).toDouble());
  return true;
}

} // namespace NS
