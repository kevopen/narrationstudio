#pragma once
#include <QIcon>
#include <QString>

// Single source for the app's icon set: modern outline SVGs shipped in
// resources/icons (no QStyle vintage shell icons, no emoji, no glyph fonts).
namespace Icons {
inline QIcon get(const QString &name) {
  // qrc prefix "/icons" + file path "icons/<name>.svg"
  return QIcon(QString(":/icons/icons/%1.svg").arg(name));
}
} // namespace Icons
