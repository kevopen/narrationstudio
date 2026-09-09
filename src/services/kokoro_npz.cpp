#include "kokoro_npz.h"
#include <algorithm>
#include <cstring>
#include <QFile>
#include <QRegularExpression>

namespace {
quint16 rd16(const uchar *p) {
  return static_cast<quint16>(p[0] | (static_cast<quint16>(p[1]) << 8));
}
quint32 rd32(const uchar *p) {
  return static_cast<quint32>(p[0]) | (static_cast<quint32>(p[1]) << 8)
       | (static_cast<quint32>(p[2]) << 16) | (static_cast<quint32>(p[3]) << 24);
}
quint64 rd64(const uchar *p) {
  return static_cast<quint64>(rd32(p))
       | (static_cast<quint64>(rd32(p + 4)) << 32);
}
} // namespace

bool NpzVoices::load(const QString &path, QString *error) {
  m_entries.clear(); m_names.clear(); m_path = path;
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    if (error) *error = "Cannot open " + path;
    return false;
  }
  const QByteArray blob = f.readAll();
  const uchar *p = reinterpret_cast<const uchar *>(blob.constData());
  qsizetype n = blob.size(), off = 0;
  while (off + 30 <= n) {
    if (rd32(p + off) != 0x04034b50) break;           // PK\x03\x04
    const quint16 method = rd16(p + off + 8);
    quint64 csize = rd32(p + off + 18);
    const quint16 nlen = rd16(p + off + 26), elen = rd16(p + off + 28);
    const qsizetype nameOff = off + 30;
    if (nameOff + nlen + elen > n) break;
    // numpy-written zips carry ZIP64 extra fields with 0xFFFFFFFF placeholders
    if (csize == 0xFFFFFFFFu) {
      qsizetype eoff = nameOff + nlen;
      const qsizetype eend = eoff + elen;
      while (eoff + 4 <= eend) {
        const quint16 id = rd16(p + eoff), sz = rd16(p + eoff + 2);
        if (id == 0x0001 && sz >= 16) {
          csize = rd64(p + eoff + 12); // packed size follows unpacked size
          break;
        }
        eoff += 4 + sz;
      }
      if (csize == 0xFFFFFFFFu) break; // unresolved size
    }
    const QString name = QString::fromUtf8(
      reinterpret_cast<const char *>(p + nameOff), nlen);
    const quint64 dataOff = static_cast<quint64>(nameOff + nlen + elen);
    if (dataOff + csize > static_cast<quint64>(n)) break;
    if (method != 0) {
      if (error) *error = "Unsupported zip compression in voices file";
      return false;
    }
    if (name.endsWith(".npy", Qt::CaseInsensitive)) {
      QString voice = name.left(name.size() - 4);
      m_entries.append({voice, dataOff, csize});
      m_names << voice;
    }
    off = static_cast<qsizetype>(dataOff + csize);
  }
  if (m_entries.isEmpty()) {
    if (error) *error = "No voices found in " + path;
    return false;
  }
  std::sort(m_names.begin(), m_names.end());
  return true;
}

bool NpzVoices::extract(const QString &voice, VoiceTensor *out, QString *error) const {
  const Entry *hit = nullptr;
  for (const auto &e : m_entries)
    if (e.name == voice) { hit = &e; break; }
  if (!hit) {
    if (error) *error = "Voice not found: " + voice;
    return false;
  }
  QFile f(m_path);
  if (!f.open(QIODevice::ReadOnly)) {
    if (error) *error = "Cannot open " + m_path;
    return false;
  }
  if (!f.seek(static_cast<qint64>(hit->dataOff))) {
    if (error) *error = "Seek failed";
    return false;
  }
  const QByteArray raw = f.read(static_cast<qint64>(hit->dataSize));
  if (raw.size() < 10 || memcmp(raw.constData(), "\x93NUMPY", 6) != 0) {
    if (error) *error = "Bad npy entry for " + voice;
    return false;
  }
  const uchar *p = reinterpret_cast<const uchar *>(raw.constData());
  const quint16 hlen = rd16(p + 8);
  if (10 + hlen > raw.size()) {
    if (error) *error = "Bad npy header for " + voice;
    return false;
  }
  const QString header = QString::fromLatin1(
    reinterpret_cast<const char *>(p + 10), hlen);
  if (!header.contains("<f4") && !header.contains("|f4")) {
    if (error) *error = "Voice tensor is not float32: " + voice;
    return false;
  }
  if (header.contains("True", Qt::CaseSensitive) && header.contains("fortran")) {
    if (error) *error = "Fortran-order voice unsupported: " + voice;
    return false;
  }
  QVector<int> dims;
  static const QRegularExpression shapeRe("\\(([^)]*)\\)");
  auto m = shapeRe.match(header);
  if (m.hasMatch()) {
    for (const QString &tok : m.captured(1).split(",", Qt::SkipEmptyParts)) {
      bool ok = false;
      const int v = tok.trimmed().toInt(&ok);
      if (ok) dims << v;
    }
  }
  if (dims.isEmpty()) {
    if (error) *error = "Cannot parse voice shape: " + voice;
    return false;
  }
  qsizetype count = 1;
  for (int d : dims) count *= d;
  const qsizetype dataOff = 10 + hlen;
  if (dataOff + count * 4 > raw.size()) {
    if (error) *error = "Truncated voice tensor: " + voice;
    return false;
  }
  const float *src = reinterpret_cast<const float *>(raw.constData() + dataOff);
  VoiceTensor t;
  t.data.resize(count);
  memcpy(t.data.data(), src, static_cast<size_t>(count) * 4);
  // Collapse to rows x 256 (e.g. (510,1,256) -> 510 x 256)
  const qsizetype total = count;
  t.cols = 256;
  t.rows = static_cast<int>(total / 256);
  if (t.rows < 1 || total % 256 != 0) {
    if (error) *error = "Unexpected voice shape for " + voice;
    return false;
  }
  *out = t;
  return true;
}
