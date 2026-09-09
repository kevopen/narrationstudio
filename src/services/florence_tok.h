#pragma once
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

// GPT-2-style byte-level BPE for Florence-2 (from tokenizer.json):
// vocab (id order) + ranked merges + added task tokens.
// Only encode() is needed for the fixed task prompt; decode() turns
// generated ids back into text (byte tokens resolved, specials skipped).
class FlorenceBpe {
public:
  bool load(const QString &tokenizerJson, QString *error = nullptr);
  bool isLoaded() const { return !m_id2tok.isEmpty(); }

  // "<MORE_DETAILED_CAPTION>" → token ids (single word, no spaces)
  QVector<int> encode(const QString &text) const;
  // generated ids → text (skips <s>/<pad>/</s>/<unk> and <...> specials)
  QString decode(const QVector<int> &ids) const;

  int taskToken(const QString &task) const; // id or -1
  int vocabSize() const { return m_id2tok.size(); }

private:
  QVector<QString> m_id2tok;          // id → token string
  QMap<QString,int> m_tok2id;
  QMap<QString,int> m_mergeRank;      // "a b" → rank
  QMap<QChar,QChar> m_byteDecode;     // unicode char → original byte char
  QVector<QChar> m_byteEncode;        // byte value → unicode char

  static QVector<QChar> byteEncoder();
  static QMap<QChar,QChar> byteDecoder(const QVector<QChar> &enc);
  QStringList splitPretok(const QString &text) const;
  QStringList splitBpeWord(const QString &mapped) const;
};
