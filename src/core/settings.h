#pragma once
#include <QObject>
#include <QSettings>
#include <QString>

// QSettings-backed studio config — replaces studio_config.json reads.
// Persists splitter layout, zoom, voice, work range, theme.
class AppSettings : public QObject {
  Q_OBJECT
public:
  explicit AppSettings(QObject *parent = nullptr);

  QString theme() const;        void setTheme(const QString &v);   // "dark"|"light"
  double pageZoom() const;      void setPageZoom(double v);
  QString voice() const;        void setVoice(const QString &v);
  QString speechVoice() const;  void setSpeechVoice(const QString &v); // Kokoro voice
  double speechSpeed() const;   void setSpeechSpeed(double v);          // 0.5..2.0
  QString geminiKey() const;    void setGeminiKey(const QString &v);
  QString geminiModel() const;  void setGeminiModel(const QString &v);
  QString geminiFallbacks() const; void setGeminiFallbacks(const QString &v);
  int chunkPages() const;         void setChunkPages(int v);
  int tokensPerMinute() const;    void setTokensPerMinute(int v);
  int requestsPerMinute() const;  void setRequestsPerMinute(int v);
  QString ollamaUrl() const;    void setOllamaUrl(const QString &v);
  QString ollamaModel() const;  void setOllamaModel(const QString &v);
  QString llmKey() const;       void setLlmKey(const QString &v);
  QStringList llmKeys() const;  void setLlmKeys(const QStringList &v);
  QStringList geminiKeys() const; void setGeminiKeys(const QStringList &v);
  QStringList describeKeys() const; void setDescribeKeys(const QStringList &v);
  QString describeModel() const;  void setDescribeModel(const QString &v);
  QString describeFallbacks() const; void setDescribeFallbacks(const QString &v);
  QString llmBase() const;      void setLlmBase(const QString &v);
  QString llmModel() const;     void setLlmModel(const QString &v);
  int workStart() const;        void setWorkStart(int v);
  int workEnd() const;          void setWorkEnd(int v);
  QByteArray mainWindowState(int version = 0) const;
  void saveMainWindowState(const QByteArray &s, int version = 0);

private:
  QSettings m_s;
};
