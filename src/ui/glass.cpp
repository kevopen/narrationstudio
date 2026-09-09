#include "glass.h"

#ifdef Q_OS_WIN
#include <windows.h>

namespace {
using SetAttrFn = HRESULT (WINAPI *)(HWND, DWORD, LPCVOID, DWORD);
SetAttrFn loadDwm() {
  static SetAttrFn fn = nullptr;
  static bool tried = false;
  if (!tried) {
    tried = true;
    if (HMODULE m = LoadLibraryW(L"dwmapi.dll"))
      fn = reinterpret_cast<SetAttrFn>(GetProcAddress(m, "DwmSetWindowAttribute"));
  }
  return fn;
}
void set(HWND hwnd, DWORD attr, auto value) {
  if (auto fn = loadDwm()) fn(hwnd, attr, &value, sizeof(value));
}
} // namespace

void Glass::apply(QWidget *window) {
  if (!window) return;
  window->winId(); // force native handle
  auto *fn = loadDwm();
  if (!fn) return;
  HWND hwnd = reinterpret_cast<HWND>(window->winId());
  const BOOL dark = TRUE;
  set(hwnd, 20, dark);              // DWMWA_USE_IMMERSIVE_DARK_MODE
  const int corners = 2;            // DWMWCP_ROUND
  set(hwnd, 33, corners);           // DWMWA_WINDOW_CORNER_PREFERENCE
  const int backdrop = 2;           // DWMSBT_MAINWINDOW (Mica-like)
  set(hwnd, 38, backdrop);          // DWMWA_SYSTEMBACKDROP_TYPE (ignored pre-22H2)
}

#else

void Glass::apply(QWidget *) {}

#endif
