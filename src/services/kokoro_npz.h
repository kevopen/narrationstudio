#pragma once
#include <QMap>
#include <QString>
#include <QVector>

// Minimal NPZ reader for kokoro voices-v1.0.bin (STORED entries of .npy
// float32 arrays). No third-party deps. Directory is parsed once; voice
// tensors are extracted on demand so only the chosen voice hits RAM.
class NpzVoices {
public:
  // rows/cols of one voice tensor (expected 510 x 256, row count varies ok)
  struct VoiceTensor { QVector<float> data; int rows = 0; int cols = 0; };

  bool load(const QString &path, QString *error = nullptr);
  QStringList names() const { return m_names; }
  bool extract(const QString &voice, VoiceTensor *out, QString *error = nullptr) const;

private:
  struct Entry { QString name; quint64 dataOff = 0; quint64 dataSize = 0; };
  QString m_path;
  QVector<Entry> m_entries;
  QStringList m_names;
};
