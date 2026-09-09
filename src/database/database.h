#pragma once
#include <QSqlDatabase>
#include <QString>

// Opens/creates the SQLite file, applies SCHEMA + _migrate (WAL on).
// Same tables as mangastudio/app/database/database.py.
class Database {
public:
  static bool open(const QString &path, QSqlDatabase &out, QString *error = nullptr);
  static bool migrate(QSqlDatabase &db, QString *error = nullptr);  // Rejects foreign files (e.g. the Python studio's studio.db): same-named
  // tables with different columns would otherwise fail later with cryptic
  // "no such column" errors on save/load.
  static bool validateSchema(QSqlDatabase &db, QString *error = nullptr);
  static QString schemaSql();
};
