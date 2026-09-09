#include "workers.h"
#include <QDateTime>

Task::Task(Fn fn, QObject *parent) : QObject(parent), m_fn(std::move(fn)) {
  setAutoDelete(true);
}
void Task::requestCancel() { m_cancel.store(true); }
void Task::run() {
  try {
    m_fn([this](int p, const QString &s){ emit progress(p, s); },
         [this]{ return m_cancel.load(); });
    if (!m_cancel.load()) emit done();
  } catch (const std::exception &e) {
    emit error(QString::fromUtf8(e.what()));
  } catch (...) { emit error("task failed"); }
}

Pace::Pace(int maxPerMinute) : m_max(maxPerMinute) {}
int Pace::waitMs(int tokens) {
  qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (now - m_windowStart > 60000) { m_windowStart = now; m_used = 0; }
  m_used += tokens;
  if (m_used > m_max) return static_cast<int>(60000 - (now - m_windowStart));
  return 0;
}
