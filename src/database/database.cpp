#include "database.h"
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QFileInfo>
#include <QDir>

QString Database::schemaSql() {
  return QStringLiteral(R"SQL(
CREATE TABLE IF NOT EXISTS projects (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, narration TEXT NOT NULL DEFAULT '', audio_path TEXT NOT NULL DEFAULT '', created_date TEXT NOT NULL DEFAULT (datetime('now')));
CREATE TABLE IF NOT EXISTS pages (id INTEGER PRIMARY KEY AUTOINCREMENT, project_id INTEGER NOT NULL REFERENCES projects(id) ON DELETE CASCADE, image_path TEXT NOT NULL, page_number INTEGER NOT NULL, description TEXT NOT NULL DEFAULT '');
CREATE TABLE IF NOT EXISTS panels (id INTEGER PRIMARY KEY AUTOINCREMENT, page_id INTEGER NOT NULL REFERENCES pages(id) ON DELETE CASCADE, x INTEGER, y INTEGER, width INTEGER, height INTEGER, order_number INTEGER, description TEXT);
CREATE TABLE IF NOT EXISTS text_blocks (id INTEGER PRIMARY KEY AUTOINCREMENT, page_id INTEGER REFERENCES pages(id) ON DELETE CASCADE, panel_id INTEGER REFERENCES panels(id) ON DELETE CASCADE, content TEXT NOT NULL, type TEXT NOT NULL DEFAULT 'dialogue', speaker_id INTEGER REFERENCES characters(id) ON DELETE SET NULL, x INTEGER, y INTEGER, width INTEGER, height INTEGER);
CREATE TABLE IF NOT EXISTS characters (id INTEGER PRIMARY KEY AUTOINCREMENT, project_id INTEGER NOT NULL REFERENCES projects(id) ON DELETE CASCADE, name TEXT NOT NULL, role TEXT, voice_style TEXT);
CREATE TABLE IF NOT EXISTS timeline (id INTEGER PRIMARY KEY AUTOINCREMENT, project_id INTEGER REFERENCES projects(id) ON DELETE CASCADE, page_id INTEGER REFERENCES pages(id) ON DELETE CASCADE, panel_id INTEGER REFERENCES panels(id) ON DELETE CASCADE, start_time REAL, end_time REAL);
CREATE TABLE IF NOT EXISTS narration_segments (id INTEGER PRIMARY KEY AUTOINCREMENT, project_id INTEGER NOT NULL REFERENCES projects(id) ON DELETE CASCADE, page_number INTEGER NOT NULL, char_start INTEGER NOT NULL, char_end INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS page_characters (page_id INTEGER NOT NULL REFERENCES pages(id) ON DELETE CASCADE, character_id INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE, PRIMARY KEY (page_id, character_id));
CREATE TABLE IF NOT EXISTS audio_markers (project_id INTEGER NOT NULL REFERENCES projects(id) ON DELETE CASCADE, idx INTEGER NOT NULL, seconds REAL NOT NULL, PRIMARY KEY (project_id, idx));
)SQL");
}

static bool hasColumn(QSqlDatabase &db, const QString &table, const QString &col) {
  QSqlQuery q(db);
  q.prepare(QString("PRAGMA table_info(%1)").arg(table));
  if (!q.exec()) return false;
  while (q.next()) if (q.value("name").toString() == col) return true;
  return false;
}

bool Database::migrate(QSqlDatabase &db, QString *error) {
  QSqlQuery q(db);
  auto exec = [&](const QString &sql) -> bool {
    if (!q.exec(sql)) { if (error) *error = q.lastError().text(); return false; }
    return true;
  };
  if (!hasColumn(db, "projects", "work_start"))
    if (!exec("ALTER TABLE projects ADD COLUMN work_start INTEGER")) return false;
  if (!hasColumn(db, "projects", "work_end"))
    if (!exec("ALTER TABLE projects ADD COLUMN work_end INTEGER")) return false;
  if (!hasColumn(db, "projects", "synopsis"))
    if (!exec("ALTER TABLE projects ADD COLUMN synopsis TEXT DEFAULT ''")) return false;
  if (!hasColumn(db, "projects", "audio_path"))
    if (!exec("ALTER TABLE projects ADD COLUMN audio_path TEXT DEFAULT ''")) return false;
  if (!exec("CREATE TABLE IF NOT EXISTS page_characters (page_id INTEGER NOT NULL REFERENCES pages(id) ON DELETE CASCADE, character_id INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE, PRIMARY KEY (page_id, character_id))")) return false;
  // Measured page-boundary seconds (timeline markers) for the voiced audio.
  if (!exec("CREATE TABLE IF NOT EXISTS audio_markers (project_id INTEGER NOT NULL REFERENCES projects(id) ON DELETE CASCADE, idx INTEGER NOT NULL, seconds REAL NOT NULL, PRIMARY KEY (project_id, idx))")) return false;
  return true;
}

bool Database::open(const QString &path, QSqlDatabase &out, QString *error) {
  QFileInfo fi(path);
  if (!fi.dir().exists()) QDir().mkpath(fi.dir().absolutePath());
  static int n = 0;
  QString connName = QString("ns_%1").arg(++n);
  out = QSqlDatabase::addDatabase("QSQLITE", connName);
  out.setDatabaseName(path);
  if (!out.open()) { if (error) *error = out.lastError().text(); return false; }
  QSqlQuery q(out);
  q.exec("PRAGMA foreign_keys = ON");
  q.exec("PRAGMA journal_mode = WAL");
  for (const QString &stmt : schemaSql().split(";", Qt::SkipEmptyParts)) {
    if (stmt.trimmed().isEmpty()) continue;
    if (!q.exec(stmt)) { if (error) *error = q.lastError().text(); return false; }
  }
  if (!migrate(out, error)) return false;
  return validateSchema(out, error);
}

bool Database::validateSchema(QSqlDatabase &db, QString *error) {
  static const QList<QPair<QString, QStringList>> required = {
    {"projects", {"name", "narration", "audio_path", "synopsis", "work_start", "work_end"}},
    {"pages", {"project_id", "image_path", "page_number", "description"}},
    {"text_blocks", {"page_id", "content", "type"}},
    {"characters", {"project_id", "name", "role", "voice_style"}},
    {"page_characters", {"page_id", "character_id"}},
    {"audio_markers", {"project_id", "idx", "seconds"}},
  };
  for (const auto &t : required) {
    QSqlQuery q(db);
    q.prepare(QString("PRAGMA table_info(%1)").arg(t.first));
    if (!q.exec()) {
      if (error) *error = q.lastError().text();
      return false;
    }
    QSet<QString> cols;
    while (q.next()) cols.insert(q.value("name").toString());
    if (cols.isEmpty()) {
      if (error) *error = "Table '" + t.first + "' is missing - database file is corrupt. Use a fresh file.";      return false;
    }
    for (const QString &c : t.second) {
      if (!cols.contains(c)) {
        if (error)
          *error = "Table '" + t.first + "' belongs to another app (no '" + c +
                   "' column). Save under a new file name instead.";
        return false;
      }
    }
  }
  return true;
}
