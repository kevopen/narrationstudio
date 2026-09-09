#include "misaki_lite.h"
#include "kokoro_g2p.h"
#include <algorithm>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace {
// U+02CC SECONDARY, U+02C8 PRIMARY
const QChar SEC(0x02CC), PRI(0x02C8);
// VOWELS: AIOQWYaiu + U+00E6,U+0251,U+0252,U+0253,U+0259,U+025B,U+025C,U+026A,U+028A,U+028C,U+1D4A
const QString VOWELS = QString::fromUtf8("AIOQWYaiu\xc3\xa6\xc9\x91\xc9\x92\xc9\x93\xc9\x99\xc9\x9b\xc9\x9c\xc9\xaa\xca\x8a\xc9\x8c\xe1\xb5\x8a");
// DIPHTHONGS: AIOQWY + U+02A4,U+02A7
const QString DIPHTHONGS = QString::fromUtf8("AIOQWY\xca\xa4\xca\xa7");
// CONSONANTS: bdfhjklmnpstvwz + U+00F0,U+014B,U+0261,U+0279,U+027E,U+0283,U+0292,U+02A4,U+02A7,U+03B8
const QString CONSONANTS = QString::fromUtf8("bdfhjklmnpstvwz\xc3\xb0\xc5\x8b\xc9\xa1\xc9\xb9\xc9\xbe\xca\x83\xca\x92\xca\xa4\xca\xa7\xce\xb8");
// US_TAUS: AIOWYiu + U+00E6,U+0251,U+0259,U+025B,U+026A,U+0279,U+028A,U+028C
const QString US_TAUS = QString::fromUtf8("AIOWYiu\xc3\xa6\xc9\x91\xc9\x99\xc9\x9b\xc9\xaa\xc9\xb9\xca\x8a\xc9\x8c");

bool isVowel(QChar c) { return VOWELS.contains(c); }

bool allOrdOk(const QString &w) {
  for (QChar c : w) {
    const uint u = c.unicode();
    if (u == 39 || u == 45) continue;
    if ((u >= 65 && u <= 90) || (u >= 97 && u <= 122)) continue;
    return false;
  }
  return true;
}
bool isAlphaWord(const QString &w) {
  if (w.isEmpty()) return false;
  for (QChar c : w) if (!c.isLetter()) return false;
  return true;
}
} // namespace

bool MisakiLite::load(const QString &dir, QString *error) {
  if (dir.isEmpty()) {
    if (error) *error = "misaki lexicon dir not found";
    return false;
  }  auto read = [&](const QString &name, QMap<QString,QString> &out, QString *err) {
    QFile f(QDir(dir).absoluteFilePath(name));
    if (!f.open(QIODevice::ReadOnly)) {
      if (err) *err = "Cannot open " + f.fileName();
      else if (error) *error = "Cannot open " + f.fileName();
      return false;
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = root.begin(); it != root.end(); ++it) {
      QString v;
      if (it.value().isString()) v = it.value().toString();
      else if (it.value().isObject()) {
        const QJsonObject o = it.value().toObject();
        v = o.contains("None") ? o["None"].toString()
                               : o.value("DEFAULT").toString();
      }
      out.insert(it.key(), v);
    }
    return true;
  };
  m_gold.clear(); m_silver.clear();
  m_goldObj.clear(); m_silverObj.clear();
  if (!read("us_gold.json", m_gold, error)) return false;
  if (!read("us_silver.json", m_silver, error)) return false;
  // Remember raw dict entries (heteronyms: NOUN/VERB/VBD...) for context picks.
  auto collect = [](const QString &path, QMap<QString,QJsonObject> &out) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = root.begin(); it != root.end(); ++it)
      if (it.value().isObject()) out.insert(it.key(), it.value().toObject());
  };
  collect(QDir(dir).absoluteFilePath("us_gold.json"), m_goldObj);
  collect(QDir(dir).absoluteFilePath("us_silver.json"), m_silverObj);
  // grow_dictionary: add capitalized/lowercase variants (original wins)
  auto grow = [](QMap<QString,QString> &d) {
    QMap<QString,QString> extra;
    for (auto it = d.begin(); it != d.end(); ++it) {
      const QString &k = it.key();
      if (k.size() < 2) continue;
      const QString lower = k.toLower();
      if (k == lower) {
        const QString cap = lower.left(1).toUpper() + lower.mid(1);
        if (k != cap && !d.contains(cap)) extra.insert(cap, it.value());
      } else if (k == lower.left(1).toUpper() + lower.mid(1)) {
        if (!d.contains(lower)) extra.insert(lower, it.value());
      }
    }
    for (auto it = extra.begin(); it != extra.end(); ++it)
      d.insert(it.key(), it.value());
  };
  grow(m_gold); grow(m_silver);
  // Same case variants for dict entries (heteronym picks need them too).
  auto growObj = [](QMap<QString,QJsonObject> &o) {
    QMap<QString,QJsonObject> extra;
    for (auto it = o.begin(); it != o.end(); ++it) {
      const QString &k = it.key();
      if (k.size() < 2) continue;
      const QString lower = k.toLower();
      if (k == lower) {
        const QString cap = lower.left(1).toUpper() + lower.mid(1);
        if (k != cap && !o.contains(cap)) extra.insert(cap, it.value());
      } else if (k == lower.left(1).toUpper() + lower.mid(1)) {
        if (!o.contains(lower)) extra.insert(lower, it.value());
      }
    }
    for (auto it = extra.begin(); it != extra.end(); ++it)
      o.insert(it.key(), it.value());
  };
  growObj(m_goldObj); growObj(m_silverObj);
  return true;
}

QString MisakiLite::applyStress(const QString &ps, bool hasStress, double stress) {
  auto restress = [](const QString &p) {
    // move each mark just before its next vowel (sorted stable by key)
    struct Item { double key; QChar ch; int ord; };
    QVector<Item> items;
    for (int i = 0; i < p.size(); ++i) items.append({double(i), p[i], i});
    for (int i = 0; i < items.size(); ++i) {
      if (items[i].ch != SEC && items[i].ch != PRI) continue;
      int j = i + 1;
      while (j < items.size() && !isVowel(items[j].ch)) ++j;
      if (j < items.size()) items[i].key = j - 0.5;
    }
    std::stable_sort(items.begin(), items.end(),
                     [](const Item &a, const Item &b){
                       return a.key != b.key ? a.key < b.key : a.ord < b.ord;
                     });
    QString out;
    for (auto &it : items) out += it.ch;
    return out;
  };
  if (!hasStress) return ps;
  const bool hasPri = ps.contains(PRI), hasSec = ps.contains(SEC);
  const auto hasVowel = [&]{ for (QChar c : ps) if (isVowel(c)) return true; return false; };
  if (stress < -1) { QString o = ps; return o.replace(PRI, "").replace(SEC, ""); }
  if (stress == -1 || ((stress == 0 || stress == -0.5) && hasPri)) {
    QString o = ps;
    return o.replace(SEC, "").replace(PRI, SEC);
  }
  if ((stress == 0 || stress == 0.5 || stress == 1) && !hasPri && !hasSec) {
    if (!hasVowel()) return ps;
    return restress(QString(SEC) + ps);
  }
  if (stress >= 1 && !hasPri && hasSec) {
    QString o = ps;
    return o.replace(SEC, PRI);
  }
  if (stress > 1 && !hasPri && !hasSec) {
    if (!hasVowel()) return ps;
    return restress(QString(PRI) + ps);
  }
  return ps;
}

bool MisakiLite::isKnown(const QString &word, const QMap<QString,QString> &gold,
                         const QMap<QString,QString> &silver) {
  if (gold.contains(word) || silver.contains(word)) return true;
  if (!isAlphaWord(word) || !allOrdOk(word)) return false;
  if (word.size() == 1) return true;
  if (word == word.toUpper() && gold.contains(word.toLower())) return true;
  return word.mid(1) == word.mid(1).toUpper();
}

QStringList MisakiLite::variants(const QString &word) {
  QStringList out;
  out << word;
  const QString lower = word.toLower();
  if (lower != word) out << lower;
  const QString cap = lower.left(1).toUpper() + lower.mid(1);
  if (cap != word && cap != lower) out << cap;
  return out;
}

QString MisakiLite::lookup(const QString &word, bool hasStress, double stress,
                           bool futureVowel, bool hasFuture,
                           const QString &prevWord, bool first) {
  Q_UNUSED(hasFuture);
  // tag-free special cases (misaki get_special_case, tag-independent parts)
  if (word == "a") return QString::fromUtf8("\xc9\x99");          // ə
  if (word == "an" || word == "An") return QString::fromUtf8("\xc3\xa6n"); // æn
  if (word == "I") return QString(SEC) + "I";
  if (word == "the" || word == "The" || word == "THE")
    return futureVowel ? QString("\u00f0i") : QString("\u00f0\u0259");
  if (word == "to" || word == "To")
    return futureVowel ? QString::fromUtf8("tu")
                       : QString::fromUtf8("t\xc9\x99");          // tu / tə
  // dictionary (gold, then silver), with case variants.
  // Dict entries (NOUN/VERB/VBD...) are picked by cheap context instead of
  // spacy tags: verb markers + sentence-initial imperatives → VERB,
  // determiners/adjectives → NOUN, pronouns → past (VBD) for narration.
  auto dictPick = [&](const QJsonObject &o) -> QString {
    static const QStringList verbs = {"to","will","would","shall","should","can",
      "could","may","might","must","do","does","did"};
    static const QStringList dets = {"the","a","an","this","that","these","those",
      "my","your","his","her","its","our","their","no","every","each","another","such"};
    static const QStringList adjSuf = {"al","ful","ous","ive","less","able","ible",
      "ic","ish","en","ant","ent"};
    static const QStringList prons = {"i","he","she","it","we","you","they"};
    const QString prev = prevWord.toLower();
    auto take = [&](const QString &k) -> QString {
      return o.contains(k) ? o[k].toString() : QString();
    };
    const bool verbCtx = verbs.contains(prev) || first;
    const bool nounCtx = dets.contains(prev) || [&]{
      for (const QString &s : adjSuf)
        if (prev.size() > s.size() + 1 && prev.endsWith(s)) return true;
      return false;
    }();
    const bool pastCtx = prons.contains(prev);
    if (verbCtx) {
      QString v = take("VERB");
      if (v.isEmpty()) v = take("VBN");
      if (v.isEmpty()) v = take("VBD");
      if (!v.isEmpty()) return applyStress(v, hasStress, stress);
    }
    if (nounCtx) {
      QString v = take("NOUN");
      if (!v.isEmpty()) return applyStress(v, hasStress, stress);
    }
    if (pastCtx) {
      QString v = take("VBD");
      if (v.isEmpty()) v = take("VBN");
      if (!v.isEmpty()) return applyStress(v, hasStress, stress);
    }
    return applyStress(take("DEFAULT"), hasStress, stress);
  };
  for (const QString &v : variants(word)) {
    auto go = m_goldObj.find(v);
    if (go != m_goldObj.end()) {
      const QString picked = dictPick(*go);
      if (!picked.isEmpty()) return picked;
    }
    auto g = m_gold.find(v);
    if (g != m_gold.end() && !g->isEmpty())
      return applyStress(*g, hasStress, stress);
    auto so = m_silverObj.find(v);
    if (so != m_silverObj.end()) {
      const QString picked = dictPick(*so);
      if (!picked.isEmpty()) return picked;
    }
    auto s = m_silver.find(v);
    if (s != m_silver.end() && !s->isEmpty())
      return applyStress(*s, hasStress, stress);
  }
  // possessive tail
  if (word.endsWith("'") && word.size() > 1) {
    const QString inner = lookup(word.left(word.size() - 1), hasStress, stress, futureVowel, hasFuture, prevWord, first);
    if (!inner.isEmpty()) return inner;
  }
  QString stem, res;
  // -s / -es / -ies
  if ((word.size() > 2 && word.endsWith('s') && !word.endsWith("ss") && isKnown(word.left(word.size()-1), m_gold, m_silver))
      || ((word.endsWith("'s") || (word.size() > 4 && word.endsWith("es"))) && isKnown(word.left(word.size()-2), m_gold, m_silver))
      || (word.size() > 4 && word.endsWith("ies") && isKnown(word.left(word.size()-3) + "y", m_gold, m_silver))) {
    if (word.size() > 2 && word.endsWith('s') && !word.endsWith("ss") && isKnown(word.left(word.size()-1), m_gold, m_silver))
      stem = word.left(word.size() - 1);
    else if (word.endsWith("'s") || (word.size() > 4 && word.endsWith("es")))
      stem = word.left(word.size() - 2);
    else
      stem = word.left(word.size() - 3) + "y";
    QString base = lookup(stem, hasStress, stress, futureVowel, hasFuture, prevWord, first);
    if (!base.isEmpty()) {
      const QChar last = base.back();
      QString suf;
      if (QString("ptkf").contains(last) || last == QChar(0x03B8)) suf = "s";
      else if (QString("sz").contains(last) || last == QChar(0x0283) || last == QChar(0x0292) || last == QChar(0x02A7) || last == QChar(0x02A4)) suf = QString(QChar(0x026A)) + "z";
      else suf = "z";
      return base + suf;
    }
  }
  // -d / -ed
  if ((word.endsWith('d') && !word.endsWith("dd") && isKnown(word.left(word.size()-1), m_gold, m_silver))
      || (word.endsWith("ed") && !word.endsWith("eed") && isKnown(word.left(word.size()-2), m_gold, m_silver))) {
    stem = (word.endsWith('d') && !word.endsWith("dd")) ? word.left(word.size()-1) : word.left(word.size()-2);
    QString base = lookup(stem, hasStress, stress, futureVowel, hasFuture, prevWord, first);
    if (!base.isEmpty()) {
      const QChar last = base.back();
      QString suf;
      if (QString("pkf").contains(last) || last == QChar(0x03B8) || last == QChar(0x0283) || last == QChar(0x02A7)) suf = "t";
      else if (last == 'd') suf = QString(QChar(0x026A)) + "d";
      else if (last != 't') suf = "d";
      else if (base.size() >= 2 && US_TAUS.contains(base[base.size()-2])) suf = QString(QChar(0x027E)) + QString(QChar(0x026A)) + "d";
      else suf = QString(QChar(0x026A)) + "d";
      return base + suf;
    }
  }
  // -ing
  static const QRegularExpression dbl("([bcdgklmnprstvxz])\\1ing$|cking$");
  QString ingStem;
  if (word.endsWith("ing") && isKnown(word.left(word.size()-3), m_gold, m_silver)) ingStem = word.left(word.size()-3);
  else if (word.endsWith("ing") && isKnown(word.left(word.size()-3) + "e", m_gold, m_silver)) ingStem = word.left(word.size()-3) + "e";
  else if (dbl.match(word).hasMatch() && isKnown(word.left(word.size()-4), m_gold, m_silver)) ingStem = word.left(word.size()-4);
  if (!ingStem.isEmpty()) {
    QString base = lookup(ingStem, hasStress, stress, futureVowel, hasFuture);
    if (!base.isEmpty()) {
      QString suf;
      if (base.size() > 1 && base.back() == 't' && US_TAUS.contains(base[base.size()-2]))
        suf = QString(QChar(0x027E)) + QString(QChar(0x026A)) + QString(QChar(0x014B));
      else
        suf = QString(QChar(0x026A)) + QString(QChar(0x014B));
      return base + suf;
    }
  }
  // ALLCAPS letter-spell (names, acronyms)
  if (word == word.toUpper() && isAlphaWord(word)) {
    QString ps;
    for (QChar c : word) {
      auto g = m_gold.find(c.toUpper());
      if (g == m_gold.end() || g->isEmpty()) return {};
      ps += *g;
    }
    QString o = applyStress(ps, true, 0);
    int cut = o.lastIndexOf(SEC);
    if (cut >= 0) { o.remove(cut, 1); o += ""; }
    // primary on the last part: replace last secondary with primary
    int at = o.lastIndexOf(SEC);
    if (at >= 0) { o[at] = PRI; return o; }
    return o;
  }
  return {};
}

QString MisakiLite::dataDir() {
  const QStringList cands = {
    QCoreApplication::applicationDirPath() + "/models/misaki",
    QCoreApplication::applicationDirPath() + "/../share/models/misaki",
    QDir::currentPath() + "/models/misaki",
#ifdef NS_SOURCE_DIR
    QString::fromLatin1(NS_SOURCE_DIR) + "/resources/misaki",
#endif
    QDir::currentPath() + "/resources/misaki",
  };
  for (const QString &d : cands)
    if (QFile::exists(QDir(d).absoluteFilePath("us_gold.json")))
      return QDir::cleanPath(d);
  return {};
}

QString MisakiLite::lookupWord(const QString &word, const QString &nextWord,
                                const QString &prevWord, bool first) {
  const QString lowered = word.toLower();
  const bool lower = (word == lowered);
  const bool upper = (word == word.toUpper()) && !lower;
  const double stress = lower ? 0.0 : (upper ? 2.0 : 0.5);
  // NOTE: misaki uses stress=None for lowercase; None + gold entry keeps ˈ.
  // Emulate by passing hasStress=false (applyStress returns ps unchanged).
  const bool hasStress = !lower;
  bool future = false, hasFuture = false;
  if (!nextWord.isEmpty()) {
    hasFuture = true;
    for (QChar c : nextWord) {
      if (!c.isLetter()) continue;
      future = QString("aeiouAEIOU").contains(c);
      break;
    }
  }
  QString ps = lookup(word, hasStress, lower ? 0.0 : stress, future, hasFuture,
                      prevWord, first);
  if (!ps.isEmpty()) return ps;
  // digits/symbols/unknowns → espeak fallback (expands numbers natively)
  if (m_fb && m_fb->isReady()) return m_fb->phonemize(word);
  return {};
}

QString MisakiLite::phonemize(const QString &sentence) {
  // Split into words, keep punctuation attached separately (outer contract:
  // punctuation survives for the Kokoro vocab filter).
  static const QString punct = QString::fromUtf8(",.!?;:\xe2\x80\xa6\xe2\x80\x94-\"'()\xe2\x80\x9c\xe2\x80\x9d\xe2\x80\x98\xe2\x80\x99");
  // word list with lookahead for the/to
  QStringList words = sentence.split(' ', Qt::SkipEmptyParts);
  // precompute cores to allow next-word lookahead
  struct Tok { QString lead, core, trail; };
  QVector<Tok> toks;
  for (const QString &w : words) {
    qsizetype lead = 0, trail = w.size();
    while (lead < w.size() && punct.contains(w[lead])) ++lead;
    while (trail > lead && punct.contains(w[trail - 1])) --trail;
    toks.append({w.left(lead), w.mid(lead, trail - lead), w.mid(trail)});
  }
  auto nextAlpha = [&](int from) -> QString {
    for (int i = from; i < toks.size(); ++i)
      if (!toks[i].core.isEmpty()) return toks[i].core;
    return {};
  };
  auto prevAlpha = [&](int at) -> QString {
    for (int i = at; i >= 0; --i)
      if (!toks[i].core.isEmpty()) return toks[i].core;
    return {};
  };
  auto isFirst = [&](int at) -> bool {
    for (int i = 0; i < at; ++i)
      if (!toks[i].core.isEmpty()) return false;
    return true;
  };
  QString out;
  for (int i = 0; i < toks.size(); ++i) {
    if (!out.isEmpty()) out += ' ';
    out += toks[i].lead;
    if (!toks[i].core.isEmpty())
      out += lookupWord(toks[i].core, nextAlpha(i + 1), prevAlpha(i - 1), isFirst(i));
    out += toks[i].trail;
  }
  return out.trimmed();
}
