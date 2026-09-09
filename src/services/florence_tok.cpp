#include "florence_tok.h"
#include <climits>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

QVector<QChar> FlorenceBpe::byteEncoder() {
  QVector<int> bs;
  for (int b = '!'; b <= '~'; ++b) bs << b;
  for (int b = 0xA1; b <= 0xAC; ++b) bs << b;
  for (int b = 0xAE; b <= 0xFF; ++b) bs << b;
  QVector<int> cs = bs;
  int n = 0;
  for (int b = 0; b < 256; ++b)
    if (!bs.contains(b)) { bs << b; cs << (256 + n); ++n; }
  // enc[byte] = unicode char, dec = inverse
  QVector<QChar> enc(256, QChar(0));
  for (int i = 0; i < 256; ++i) enc[bs[i]] = QChar(cs[i]);
  return enc;
}

QMap<QChar,QChar> FlorenceBpe::byteDecoder(const QVector<QChar> &enc) {
  QMap<QChar,QChar> dec;
  for (int b = 0; b < 256; ++b) dec.insert(enc[b], QChar(b));
  return dec;
}

bool FlorenceBpe::load(const QString &tokenizerJson, QString *error) {
  QFile f(tokenizerJson);
  if (!f.open(QIODevice::ReadOnly)) {
    if (error) *error = "Cannot open " + tokenizerJson;
    return false;
  }
  const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
  const QJsonObject model = root["model"].toObject();
  const QJsonObject vocab = model["vocab"].toObject();
  if (vocab.isEmpty()) {
    if (error) *error = "tokenizer has no vocab";
    return false;
  }
  // vocab: token → id; invert to id order
  QMap<int,QString> byId;
  int top = 0;
  for (auto it = vocab.begin(); it != vocab.end(); ++it) {
    const int id = it.value().toInt();
    byId.insert(id, it.key());
    top = qMax(top, id);
  }
  m_id2tok.resize(top + 1);
  for (auto it = byId.begin(); it != byId.end(); ++it)
    m_id2tok[it.key()] = it.value();
  m_tok2id.clear();
  for (int i = 0; i < m_id2tok.size(); ++i)
    if (!m_id2tok[i].isEmpty()) m_tok2id.insert(m_id2tok[i], i);
  // added tokens (task prompts, specials) override plain ids
  for (const QJsonValue &v : root["added_tokens"].toArray()) {
    const QJsonObject o = v.toObject();
    const int id = o["id"].toInt();
    const QString c = o["content"].toString();
    if (id >= 0 && !c.isEmpty()) {
      if (id >= m_id2tok.size()) m_id2tok.resize(id + 1);
      m_id2tok[id] = c;
      m_tok2id.insert(c, id);
    }
  }
  m_mergeRank.clear();
  int rank = 0;
  for (const QJsonValue &v : model["merges"].toArray()) {
    const QString m = v.toString().trimmed();
    const int sp = m.indexOf(' ');
    if (sp > 0) m_mergeRank.insert(m, rank);
    ++rank;
  }
  m_byteEncode = byteEncoder();
  m_byteDecode = byteDecoder(m_byteEncode);
  return true;
}

int FlorenceBpe::taskToken(const QString &task) const {
  auto it = m_tok2id.find(task);
  return it != m_tok2id.end() ? it.value() : -1;
}

QStringList FlorenceBpe::splitPretok(const QString &text) const {
  // Standard GPT-2 pre-tokenizer: contractions, letter/number runs with
  // optional leading space, punctuation runs, whitespace runs.
  static const QRegularExpression re(
    "'s|'t|'re|'ve|'m|'ll|'d| ?\\p{L}+| ?\\p{N}+"
    "| ?[^\\s\\p{L}\\p{N}]+|\\s+(?!\\S)|\\s+");
  QStringList out;
  auto it = re.globalMatch(text);
  while (it.hasNext()) out << it.next().captured(0);
  return out;
}

QStringList FlorenceBpe::splitBpeWord(const QString &mapped) const {
  // `mapped` is already byte-encoded (Ġ for space); merge lowest rank first.
  QStringList parts;
  for (const QChar ch : mapped) parts << QString(ch);
  // Highest-priority merge first, like tokenizers' BPE loop
  while (parts.size() > 1) {
    int bestRank = INT_MAX, bestAt = -1;
    for (int i = 0; i + 1 < parts.size(); ++i) {
      auto it = m_mergeRank.find(parts[i] + ' ' + parts[i + 1]);
      if (it != m_mergeRank.end() && it.value() < bestRank) {
        bestRank = it.value(); bestAt = i;
      }
    }
    if (bestAt < 0) break;
    parts[bestAt] = parts[bestAt] + parts[bestAt + 1];
    parts.removeAt(bestAt + 1);
  }
  return parts;
}

QVector<int> FlorenceBpe::encode(const QString &text) const {
  QVector<int> ids;
  for (const QString &seg : splitPretok(text)) {
    if (m_tok2id.contains(seg)) { ids << m_tok2id[seg]; continue; }
    // byte-encode the segment, then BPE-merge within it only
    QString mapped;
    const QByteArray sb = seg.toUtf8();
    for (char b : sb) mapped += m_byteEncode[static_cast<uchar>(b)];
    for (const QString &p : splitBpeWord(mapped)) {
      auto it = m_tok2id.find(p);
      if (it != m_tok2id.end()) ids << it.value();
      // byte_fallback=false: drop unknown pieces (matches reference)
    }
  }
  return ids;
}

QString FlorenceBpe::decode(const QVector<int> &ids) const {
  QString text;
  for (int id : ids) {
    if (id < 0 || id >= m_id2tok.size()) continue;
    const QString &tok = m_id2tok[id];
    if (tok.isEmpty()) continue;
    if (id <= 3) continue;                       // <s>/<pad>/</s>/<unk>
    if (tok.startsWith('<') && tok.endsWith('>')) continue; // <loc_*>, tasks…
    text += tok;
  }
  // Resolve byte-level tokens back to bytes, Ġ (U+0120) → space.
  QByteArray bytes;
  for (const QChar ch : text) {
    if (ch == QChar(0x0120)) { bytes += ' '; continue; }
    auto it = m_byteDecode.find(ch);
    bytes += (it != m_byteDecode.end()) ? (char)it.value().unicode() : '?';
  }
  return QString::fromUtf8(bytes).trimmed();
}
