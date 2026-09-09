#pragma once
#include <QApplication>
// Applies resources/theme.qss once. No per-widget stylesheets (except timeline).
namespace Theme {
void applyDark(QApplication &app);
void applyLight(QApplication &app);
}
