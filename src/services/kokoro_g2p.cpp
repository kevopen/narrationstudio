#include "kokoro_g2p.h"
#include <QMutexLocker>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

// speak_lib.h constants (stable ABI)
enum {
  ESPEAK_CHARS_UTF8 = 1,
  ESPEAK_PHONEMES_IPA = 0x02,
  ESPEAK_PHONEMES_TIE = 0x80,
  ESPEAK_AUDIO_SYNC = 2
};

struct EspeakG2P::Api {
  int (*initialize)(int, int, const char *, int) = nullptr;
  int (*setVoiceByName)(const char *) = nullptr;
  const char *(*textToPhonemes)(const void **, int, int) = nullptr;
  int (*terminate)() = nullptr;
};

static void *loadLib(const QString &path) {
#ifdef Q_OS_WIN
  return reinterpret_cast<void *>(LoadLibraryW(
    reinterpret_cast<const wchar_t *>(path.utf16())));
#else
  return dlsym(RTLD_DEFAULT, "noop"); // placeholder, Linux loads below
#endif
}

EspeakG2P::EspeakG2P(const QString &libPath, const QString &dataPath) {
  m_api = new Api();
  m_lib = loadLib(libPath);
#ifndef Q_OS_WIN
  if (!m_lib) m_lib = dlopen(libPath.toUtf8().constData(), RTLD_NOW);
#endif
  if (!m_lib) {
    m_error = "Cannot load " + libPath;
    return;
  }
  auto sym = [&](const char *name) -> void * {
#ifdef Q_OS_WIN
    return reinterpret_cast<void *>(GetProcAddress(
      reinterpret_cast<HMODULE>(m_lib), name));
#else
    return dlsym(m_lib, name);
#endif
  };
  m_api->initialize = reinterpret_cast<decltype(m_api->initialize)>(
    sym("espeak_Initialize"));
  m_api->setVoiceByName = reinterpret_cast<decltype(m_api->setVoiceByName)>(
    sym("espeak_SetVoiceByName"));
  m_api->textToPhonemes = reinterpret_cast<decltype(m_api->textToPhonemes)>(
    sym("espeak_TextToPhonemes"));
  m_api->terminate = reinterpret_cast<decltype(m_api->terminate)>(
    sym("espeak_Terminate"));
  if (!m_api->initialize || !m_api->setVoiceByName || !m_api->textToPhonemes) {
    m_error = "espeak-ng.dll is missing exports";
    return;
  }
  const QByteArray data = dataPath.toUtf8();
  if (m_api->initialize(ESPEAK_AUDIO_SYNC, 0, data.constData(), 0) < 0) {
    m_error = "espeak_Initialize failed (data path?)";
    return;
  }
  if (m_api->setVoiceByName("en-us") != 0) {
    m_error = "en-us voice not found in espeak data";
    return;
  }
  // Warm-up call: verifies flags produce IPA output with stress marks.
  const char *probe = "Hello.";
  const void *pp = probe;
  const char *out = m_api->textToPhonemes(&pp, ESPEAK_CHARS_UTF8,
                                          ESPEAK_PHONEMES_IPA | ESPEAK_PHONEMES_TIE);
  const QString s = QString::fromUtf8(out ? out : "");
  if (!s.contains(QString::fromUtf8("ˈ")) && !s.contains("h")) {
    m_error = "espeak smoke test gave no phonemes: " + s;
    return;
  }
  m_ready = true;
}

EspeakG2P::~EspeakG2P() {
  if (m_api && m_api->terminate && m_ready) m_api->terminate();
  delete m_api;
#ifdef Q_OS_WIN
  if (m_lib) FreeLibrary(reinterpret_cast<HMODULE>(m_lib));
#else
  if (m_lib && m_lib != RTLD_DEFAULT) dlclose(m_lib);
#endif
}

QString EspeakG2P::phonemize(const QString &text) {
  QMutexLocker lock(&m_mutex);
  if (!m_ready) return {};
  // espeak consumes clause-boundary punctuation, but Kokoro's vocab uses
  // ",.!?" as pause tokens (like phonemizer's preserve_punctuation):
  // split words, phonemize the core, re-attach punctuation.
  static const QString punct = QString::fromUtf8(",.!?;:…—-\"'()“”‘’");
  QString out;
  for (const QString &word : text.split(' ', Qt::SkipEmptyParts)) {
    qsizetype lead = 0, trail = word.size();
    while (lead < word.size() && punct.contains(word[lead])) ++lead;
    while (trail > lead && punct.contains(word[trail - 1])) --trail;
    const QString core = word.mid(lead, trail - lead);
    QString ph;
    if (!core.isEmpty()) {
      const QByteArray utf8 = core.toUtf8();
      // One call emits one clause and advances pos — loop to the terminator.
      const void *pos = utf8.constData();
      while (pos && *static_cast<const char *>(pos)) {
        const char *chunk = m_api->textToPhonemes(
          &pos, ESPEAK_CHARS_UTF8, ESPEAK_PHONEMES_IPA | ESPEAK_PHONEMES_TIE);
        if (!chunk || !*chunk) break;
        if (!ph.isEmpty()) ph += ' ';
        ph += QString::fromUtf8(chunk);
      }
    }
    if (!out.isEmpty()) out += ' ';
    out += word.left(lead) + ph + word.mid(trail);
  }
  return out.trimmed();
}

QString EspeakG2P::filterVocab(const QString &phonemes,
                               const QMap<QString,int> &vocab) {
  QString out;
  out.reserve(phonemes.size());
  bool lastSpace = true; // strip leading space
  for (const QChar ch : phonemes) {
    const QString key(ch);
    if (!vocab.contains(key)) {
      if (ch.isSpace()) lastSpace = true;
      continue;
    }
    if (key == " ") {
      if (lastSpace) continue;
      lastSpace = true;
      out += ' ';
    } else {
      lastSpace = false;
      out += key;
    }
  }
  while (!out.isEmpty() && out.back() == ' ') out.chop(1);
  return out;
}
