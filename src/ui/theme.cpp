#include "theme.h"
#include <QFile>

namespace Theme {
static void applyFile(QApplication &app, const QString &path) {
  QFile f(path);
  if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    app.setStyleSheet(QString::fromUtf8(f.readAll()));
    return;
  }
  QFile r(":/theme.qss");
  if (r.open(QIODevice::ReadOnly | QIODevice::Text))
    app.setStyleSheet(QString::fromUtf8(r.readAll()));
}
void applyDark(QApplication &app) { applyFile(app, "resources/theme.qss"); }
void applyLight(QApplication &app) {
  // Light = dark file with palette swap for now; full light QSS later.
  applyFile(app, "resources/theme.qss");
  app.setStyleSheet(app.styleSheet() + "\nQMainWindow,QDialog{background:#f4f6fa;color:#1a2030;}");
}
}
