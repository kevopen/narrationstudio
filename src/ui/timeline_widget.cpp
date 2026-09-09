#include "timeline_widget.h"
#include <algorithm>
#include <cmath>
#include <QAudioDecoder>
#include <QMouseEvent>
#include <QPainter>
#include <QFile>

TimelineWidget::TimelineWidget(QWidget *parent) : QWidget(parent) {
  setObjectName("timelineWidget");
  setMinimumHeight(120);
  m_player = new QMediaPlayer(this);
  m_out = new QAudioOutput(this);
  m_player->setAudioOutput(m_out);
  connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 ms){
    m_pos = ms / 1000.0;
    emit positionChanged(m_pos, m_total);
    const int pg = pageAt(m_pos);
    if (pg != m_curPage) { m_curPage = pg; emit pageChanged(pg); }
    update();
  });
  connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 ms){
    if (ms > 0) { m_total = ms / 1000.0; update(); }
  });
  connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this]{
    emit playbackToggled(isPlaying());
  });
}

void TimelineWidget::togglePlay() {
  if (m_player->source().isEmpty()) return;
  if (isPlaying()) m_player->pause();
  else m_player->play();
}

void TimelineWidget::stopPlayback() {
  m_player->stop();
  m_pos = 0;
  m_curPage = -1;
  update();
}

bool TimelineWidget::isPlaying() const {
  return m_player->playbackState() == QMediaPlayer::PlayingState;
}

int TimelineWidget::pageAt(double seconds) const {
  const int pages = qMax(1, m_pages);
  int idx = 0;
  for (double m : m_markers) {
    if (seconds >= m - 1e-6) idx++;
    else break;
  }
  return qBound(0, idx, pages - 1);
}

void TimelineWidget::setAudio(const QString &wavPath) {
  m_player->setSource(QUrl::fromLocalFile(wavPath));
  m_pos = 0;
  m_curPage = -1;
  // Duration straight from the WAV header (immediate + exact; the player's
  // duration arrives async and only refines it).
  QFile probe(wavPath);
  if (probe.open(QIODevice::ReadOnly) && probe.size() > 44) {
    QByteArray hdr = probe.read(44);
    const uchar *p = reinterpret_cast<const uchar *>(hdr.constData());
    auto r32 = [&](int o){ return quint32(p[o]) | (quint32(p[o+1])<<8) | (quint32(p[o+2])<<16) | (quint32(p[o+3])<<24); };
    const quint32 rate = r32(24), bytes = r32(40);
    if (rate >= 1000 && bytes > 0) m_total = double(bytes) / 2 / rate;
  }
  m_player->setSource(QUrl::fromLocalFile(wavPath));
  // Fast peak compute: read raw 16-bit mono like Python compute_waveform.
  m_peaks.clear();
  QFile f(wavPath);
  if (f.open(QIODevice::ReadOnly)) {
    QByteArray raw = f.readAll();
    int dataAt = raw.indexOf("data");
    if (dataAt > 0) {
      const int bins = 1200;
      int total = (raw.size() - dataAt - 8) / 2;
      int chunk = qMax(1, total / bins);
      for (int b = 0; b < bins && (dataAt + 8 + (b+1)*chunk*2) <= raw.size(); ++b) {
        double sum = 0;
        const char *p = raw.constData() + dataAt + 8 + b * chunk * 2;
        for (int i = 0; i < chunk; ++i) {
          qint16 s = qint16(quint8(p[2*i]) | (quint8(p[2*i+1]) << 8));
          sum += double(s) * s;
        }
        m_peaks.append(float(qMin(1.0, sqrt(sum / chunk) / 32768.0 * 3.0)));
      }
    }
  }
  update();
}

void TimelineWidget::setMarkers(const QVector<double> &m) { m_markers = m; update(); }
void TimelineWidget::setDurations(const QVector<double> &d, double total) {
  m_total = total; m_markers.clear();
  double acc = 0;
  for (int i = 0; i + 1 < d.size(); ++i) { acc += d[i]; m_markers.append(acc); }
  update();
}
void TimelineWidget::setPageCount(int n) { m_pages = n; update(); }

static double xToSec(double x, double w, double total) { return total <= 0 ? 0 : x / w * total; }
static double secToX(double s, double w, double total) { return total <= 0 ? 0 : s / total * w; }

void TimelineWidget::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.fillRect(rect(), QColor("#0f1320"));
  // waveform
  p.setPen(Qt::NoPen); p.setBrush(QColor("#4c7a8a"));
  int n = m_peaks.size(), h = height() - 24;
  for (int i = 0; i < n; ++i) {
    int x = static_cast<int>(double(i) / qMax(1, n) * width());
    int bh = static_cast<int>(m_peaks[i] * h);
    p.drawRect(x, (h - bh) / 2, qMax(1, width() / qMax(1, n)), bh);
  }
  // page-boundary markers: vivid orange, 3px, with page numbers —
  // unmistakable against the teal waveform and green playhead.
  QFont tagFont = p.font();
  tagFont.setPixelSize(11);
  tagFont.setBold(true);
  p.setFont(tagFont);
  for (int i = 0; i < m_markers.size(); ++i) {
    const int x = static_cast<int>(secToX(m_markers[i], width(), m_total));
    p.setPen(QPen(QColor("#ff9f1c"), 3));
    p.drawLine(x, 0, x, height() - 20);
    p.setPen(QColor("#ffb85c"));
    p.drawText(x + 4, 14, QString::number(i + 2)); // marker i starts page i+2
  }
  // playhead: bright green
  const int px = static_cast<int>(secToX(m_pos, width(), m_total));
  p.setPen(QPen(QColor("#3ddc84"), 2));
  p.drawLine(px, 0, px, height() - 20);
  p.setPen(QColor("#9aa8c0"));
  p.drawText(8, height() - 6, QString("%1s  |  %2 pages  |  %3 markers  -  drag to add/move, button below to play")
             .arg(m_total, 0, 'f', 1).arg(m_pages).arg(m_markers.size()));
}

void TimelineWidget::mousePressEvent(QMouseEvent *e) {
  double sec = xToSec(e->position().x(), width(), m_total);
  // move nearest marker within 8px, else insert
  int best = -1; double bestD = 8.0 / width() * m_total;
  for (int i = 0; i < m_markers.size(); ++i) {
    double d = qAbs(m_markers[i] - sec);
    if (d < bestD) { bestD = d; best = i; }
  }
  if (best >= 0) m_markers[best] = sec;
  else { m_markers.append(sec); std::sort(m_markers.begin(), m_markers.end()); }
  emit markersChanged(m_markers);
  update();
}
void TimelineWidget::mouseMoveEvent(QMouseEvent *e) {
  if (e->buttons() & Qt::LeftButton) mousePressEvent(e);
}
