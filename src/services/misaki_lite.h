#pragma once
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

// misaki-lite: lexicon-first English G2P, ported from hexgrad/misaki (MIT).
// Common words come from curated gold/silver dictionaries with hand-placed
// stress (the, to, sudden, nation...); unknowns fall back to espeak.
// This is what Python's KPipeline uses — raw espeak for everything sounds
// choppy/wrong by comparison ("su den" instead of "sudden").
// spacy POS tagging is replaced by small heuristics (caps + next-word vowel
// lookahead); numbers/symbols go to espeak, which expands them natively.
class EspeakG2P;

class MisakiLite {
public:
  bool load(const QString &dir, QString *error = nullptr); // us_gold/silver.json
  bool isLoaded() const { return !m_gold.isEmpty(); }
  void setFallback(EspeakG2P *g) { m_fb = g; } // not owned (may be null)

  // Locate the lexicon data (installed models/misaki, dev resources/misaki).
  static QString dataDir();

  // Whole sentence → phoneme string, punctuation preserved for the vocab filter.
  QString phonemize(const QString &sentence);

  // Test hooks
  QString lookupWord(const QString &word, const QString &nextWord = {},
                     const QString &prevWord = {}, bool first = false);
  static QString applyStress(const QString &ps, bool hasStress, double stress);

private:
  QMap<QString,QString> m_gold, m_silver; // expanded, dicts resolved
  QMap<QString,QJsonObject> m_goldObj, m_silverObj; // raw dict entries
  EspeakG2P *m_fb = nullptr;

  QString lookup(const QString &word, bool hasStress, double stress,
                 bool futureVowel, bool hasFuture,
                 const QString &prevWord = {}, bool first = false);
  static bool isKnown(const QString &word, const QMap<QString,QString> &gold,
                      const QMap<QString,QString> &silver);
  static QStringList variants(const QString &word);
};
