#include "settings.h"
#include <QRegularExpression>
AppSettings::AppSettings(QObject *parent)
  : QObject(parent), m_s(QStringLiteral("NarrationStudio"), QStringLiteral("Studio")) {}

QString AppSettings::theme() const { return m_s.value("ui/theme", "dark").toString(); }
void AppSettings::setTheme(const QString &v) { m_s.setValue("ui/theme", v); }
double AppSettings::pageZoom() const { return m_s.value("viewer/zoom", 1.0).toDouble(); }
void AppSettings::setPageZoom(double v) { m_s.setValue("viewer/zoom", v); }
QString AppSettings::voice() const { return m_s.value("tts/voice", "narrator").toString(); }
void AppSettings::setVoice(const QString &v) { m_s.setValue("tts/voice", v); }
QString AppSettings::speechVoice() const { return m_s.value("tts/kokoroVoice", "af_heart").toString(); }
void AppSettings::setSpeechVoice(const QString &v) { m_s.setValue("tts/kokoroVoice", v); }
double AppSettings::speechSpeed() const { return m_s.value("tts/speed", 1.0).toDouble(); }
void AppSettings::setSpeechSpeed(double v) { m_s.setValue("tts/speed", v); }
QString AppSettings::geminiKey() const { return m_s.value("ai/geminiKey").toString(); }
void AppSettings::setGeminiKey(const QString &v) { m_s.setValue("ai/geminiKey", v); }
QString AppSettings::geminiModel() const { return m_s.value("ai/geminiModel", "gemini-3.6-flash").toString(); }
void AppSettings::setGeminiModel(const QString &v) { m_s.setValue("ai/geminiModel", v); }
QString AppSettings::geminiFallbacks() const { return m_s.value("ai/geminiFallbacks", "gemini-3.7-flash,gemini-3.8-flash,gemini-3.5-flash,gemini-flash-latest").toString(); }
void AppSettings::setGeminiFallbacks(const QString &v) { m_s.setValue("ai/geminiFallbacks", v); }
int AppSettings::chunkPages() const { return m_s.value("ai/chunkPages", 100).toInt(); }
void AppSettings::setChunkPages(int v) { m_s.setValue("ai/chunkPages", v); }
int AppSettings::tokensPerMinute() const { return m_s.value("ai/tokensPerMinute", 5500).toInt(); }
void AppSettings::setTokensPerMinute(int v) { m_s.setValue("ai/tokensPerMinute", v); }
int AppSettings::requestsPerMinute() const { return m_s.value("ai/requestsPerMinute", 18).toInt(); }
void AppSettings::setRequestsPerMinute(int v) { m_s.setValue("ai/requestsPerMinute", v); }
QString AppSettings::ollamaUrl() const { return m_s.value("ai/ollamaUrl", "http://localhost:11434").toString(); }
void AppSettings::setOllamaUrl(const QString &v) { m_s.setValue("ai/ollamaUrl", v); }
QString AppSettings::ollamaModel() const { return m_s.value("ai/ollamaModel", "llama3.2").toString(); }
void AppSettings::setOllamaModel(const QString &v) { m_s.setValue("ai/ollamaModel", v); }
QString AppSettings::llmKey() const { return m_s.value("ai/llmKey").toString(); }
void AppSettings::setLlmKey(const QString &v) { m_s.setValue("ai/llmKey", v); }
// Key pools: one per line (commas/semicolons also split). Legacy single-key
// values are honored when the pool is empty, so old setups keep working.
static QStringList splitKeys(const QString &s) {
  QStringList out;
  for (const QString &t : s.split(QRegularExpression("[\\n,;]+"), Qt::SkipEmptyParts)) {
    const QString k = t.trimmed();
    if (!k.isEmpty() && !out.contains(k)) out << k;
  }
  return out;
}
QStringList AppSettings::llmKeys() const {
  QStringList pool = splitKeys(m_s.value("ai/llmKeys").toString());
  if (pool.isEmpty()) {
    const QString single = llmKey().trimmed();
    if (!single.isEmpty()) pool << single;
  }
  return pool;
}
void AppSettings::setLlmKeys(const QStringList &v) { m_s.setValue("ai/llmKeys", v.join("\n")); }
QStringList AppSettings::geminiKeys() const {
  QStringList pool = splitKeys(m_s.value("ai/geminiKeys").toString());
  if (pool.isEmpty()) {
    const QString single = geminiKey().trimmed();
    if (!single.isEmpty()) pool << single;
  }
  return pool;
}
void AppSettings::setGeminiKeys(const QStringList &v) { m_s.setValue("ai/geminiKeys", v.join("\n")); }
QString AppSettings::llmBase() const { return m_s.value("ai/llmBase", "https://api.groq.com/openai/v1").toString(); }
void AppSettings::setLlmBase(const QString &v) { m_s.setValue("ai/llmBase", v); }
QString AppSettings::llmModel() const { return m_s.value("ai/llmModel", "llama-3.1-8b-instant").toString(); }
void AppSettings::setLlmModel(const QString &v) { m_s.setValue("ai/llmModel", v); }
int AppSettings::workStart() const { return m_s.value("work/start", 1).toInt(); }
void AppSettings::setWorkStart(int v) { m_s.setValue("work/start", v); }
int AppSettings::workEnd() const { return m_s.value("work/end", 0).toInt(); }
void AppSettings::setWorkEnd(int v) { m_s.setValue("work/end", v); }
QByteArray AppSettings::mainWindowState(int version) const {
  return m_s.value(QString("ui/state%1").arg(version)).toByteArray();
}
void AppSettings::saveMainWindowState(const QByteArray &s, int version) {
  m_s.setValue(QString("ui/state%1").arg(version), s);
}
