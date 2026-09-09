#pragma once
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QVector>
#include <QWidget>

// Waveform + high-visibility page-boundary markers + transport.
// Bottom dock; objectName = "timelineWidget" (theme override kept).
// Emits pageChanged() during playback so the stage flips panels in sync.
class TimelineWidget : public QWidget {
  Q_OBJECT
public:
  explicit TimelineWidget(QWidget *parent = nullptr);
  void setAudio(const QString &wavPath);          // loads + computes peak bins
  void setMarkers(const QVector<double> &m);     // page boundaries (seconds)
  QVector<double> markers() const { return m_markers; }
  void setDurations(const QVector<double> &d, double total);
  void setPageCount(int n);
  double duration() const { return m_total; }
  // Transport (drives the shared preview/audio player)
  void togglePlay();
  void stopPlayback();
  bool isPlaying() const;
  int pageAt(double seconds) const; // 0-based page index for a timestamp
signals:
  void markersChanged(const QVector<double> &m);
  void pageChanged(int index);       // follow-playback flips the stage
  void positionChanged(double sec, double total);
  void playbackToggled(bool playing);
private:
  void paintEvent(QPaintEvent *) override;
  void mousePressEvent(QMouseEvent *) override;
  void mouseMoveEvent(QMouseEvent *) override;
  QVector<float> m_peaks; QVector<double> m_markers;
  double m_total = 0; int m_pages = 0; double m_pos = 0;
  int m_curPage = -1;
  QMediaPlayer *m_player = nullptr; QAudioOutput *m_out = nullptr;
};
