#pragma once
#include <QWidget>

// Native window polish: dark titlebar + rounded frame corners + Mica/Acrylic
// backdrop on Windows 11 (dwmapi loaded dynamically — no link dependency).
// No-op on other platforms. Call once per top-level window.
namespace Glass {
void apply(QWidget *window);
}
