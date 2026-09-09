#pragma once
#include "core/project.h"
#include <QSqlDatabase>

namespace NS {
// Single-transaction save/load (no Python GIL stalls). Preserves
// blocks/descriptions/cast; incremental episode append keeps old rows.
class ProjectStore {
public:
  static bool save(const QString &dbPath, const Project &p, QString *error = nullptr);
  static bool load(const QString &dbPath, Project &p, QString *error = nullptr);
};
} // namespace NS
