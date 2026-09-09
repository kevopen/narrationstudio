#pragma once
#include <atomic>
#include <functional>
#include <QFutureWatcher>
#include <QObject>
#include <QRunnable>
#include <QThreadPool>

// Every heavy step = QRunnable with progress/status/error/done + cancel.
// Improvement over Python QThread-per-worker: global QThreadPool idealThreadCount.
class Task : public QObject, public QRunnable {
  Q_OBJECT
public:
  using Fn = std::function<void(std::function<void(int,QString)> progress,
                                std::function<bool()> cancelled)>;
  explicit Task(Fn fn, QObject *parent = nullptr);
  void run() override;
  void requestCancel();
signals:
  void progress(int pct, const QString &status);
  void done();
  void error(const QString &msg);
private:
  Fn m_fn; std::atomic<bool> m_cancel{false};
};

class Pace {  // token/request window pacer (mirrors workers.py _TokenWindowPacer)
public:
  explicit Pace(int maxPerMinute);
  int waitMs(int tokens = 1);  // returns ms to sleep (0 = go)
private:
  int m_max; qint64 m_windowStart = 0; int m_used = 0;
};
