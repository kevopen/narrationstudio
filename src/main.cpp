#include <QApplication>
#include <QThreadPool>
#include "ui/main_window.h"
#include "ui/theme.h"

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  app.setApplicationName("NarrationStudio");
  app.setOrganizationName("NarrationStudio");
  Theme::applyDark(app);
  // Thread pool: leave one core for the GUI (same idea as Python's pool sizing).
  QThreadPool::globalInstance()->setMaxThreadCount(
    qMax(2, QThreadPool::globalInstance()->maxThreadCount() - 1));
  MainWindow w;
  w.show();
  return app.exec();
}
