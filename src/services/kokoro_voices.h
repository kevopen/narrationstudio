#pragma once
#include <QMap>
#include <QStringList>

// Canonical Kokoro v1.0 voice names (54). Used for the picker before the
// voices file is read; the engine replaces this with the file's keys.
inline QStringList kokoroDefaultVoices() {
  return {
    "af_alloy", "af_aoede", "af_bella", "af_heart", "af_jessica", "af_kore",
    "af_nicole", "af_nova", "af_river", "af_sarah", "af_sky",
    "am_adam", "am_echo", "am_eric", "am_fenrir", "am_liam", "am_michael",
    "am_onyx", "am_puck", "am_santa",
    "bf_alice", "bf_emma", "bf_isabella", "bf_lily",
    "bm_daniel", "bm_fable", "bm_george", "bm_lewis",
    "ef_dora", "em_alex", "em_santa", "ff_siwis",
    "hf_alpha", "hf_beta", "hm_omega", "hm_psi",
    "if_sara", "im_nicola",
    "jf_alpha", "jf_gongitsune", "jf_nezumi", "jf_tebukuro", "jm_kumo",
    "pf_dora", "pm_alex", "pm_santa",
    "zf_xiaobei", "zf_xiaoni", "zf_xiaoxiao", "zf_xiaoyi",
    "zm_yunjian", "zm_yunxi", "zm_yunxia", "zm_yunyang"
  };
}

// Curated grades/labels (mirror Python's KOKORO_VOICES): unknown ids show bare.
inline QMap<QString,QString> kokoroVoiceLabels() {
  return {
    {"af_bella", "Female US, warm expressive (top rated)"},
    {"af_heart", "Female US, soft narrator (top rated)"},
    {"af_nicole", "Female US, clean studio-grade"},
    {"af_sarah", "Female US, smooth storyteller"},
    {"af_aoede", "Female US, resonant"},
    {"af_kore", "Female US, calm"},
    {"af_nova", "Female US, neutral"},
    {"af_alloy", "Female US, youthful"},
    {"af_sky", "Female US, bright, short training"},
    {"af_jessica", "Female US, lighter quality"},
    {"af_river", "Female US, lighter quality"},
    {"am_michael", "Male US, deep authoritative"},
    {"am_fenrir", "Male US, strong gravelly"},
    {"am_puck", "Male US, energetic story voice"},
    {"am_onyx", "Male US, low moody"},
    {"am_echo", "Male US, neutral"},
    {"am_eric", "Male US, neutral"},
    {"am_liam", "Male US, neutral"},
    {"am_adam", "Male US, ROUGH quality"},
    {"am_santa", "Male US, novelty"},
    {"bf_emma", "Female UK, elegant (top British)"},
    {"bf_isabella", "Female UK, refined"},
    {"bf_alice", "Female UK, neutral"},
    {"bf_lily", "Female UK, lighter quality"},
    {"bm_george", "Male UK, classic narrator"},
    {"bm_fable", "Male UK, storytelling"},
    {"bm_daniel", "Male UK, neutral"},
    {"bm_lewis", "Male UK, lighter quality"},
    {"jf_alpha", "Female Japanese"},
    {"jm_kumo", "Male Japanese"},
  };
}

inline QString kokoroVoiceDisplay(const QString &id) {
  static const QMap<QString,QString> labels = kokoroVoiceLabels();
  auto it = labels.find(id);
  return it != labels.end() ? id + " - " + it.value() : id;
}
