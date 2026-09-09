#pragma once
#include "engines.h"
#include <QNetworkAccessManager>

// Cloud/local LLM narrators over HTTP (work with zero models installed).
// Gemini: POST generativelanguage.googleapis.com with API key.
// Ollama: POST /api/generate. Both honor token/request pacing in ui/workers.
class GeminiNarrator : public INarrator {
  Q_OBJECT
public:
  explicit GeminiNarrator(const QString &apiKey, QObject *parent = nullptr);
  QString narrate(const QString &prompt, QString *error = nullptr) override;
private:
  QString m_key;
};

class OllamaNarrator : public INarrator {
  Q_OBJECT
public:
  explicit OllamaNarrator(const QString &baseUrl, const QString &model, QObject *parent = nullptr);
  QString narrate(const QString &prompt, QString *error = nullptr) override;
private:
  QString m_base, m_model;
};

// Any OpenAI-compatible chat API (Groq/OpenRouter/... and Google's
// OpenAI endpoint for Gemini 3.x): POST {base}/chat/completions.
class OpenAiCompatNarrator : public INarrator {
  Q_OBJECT
public:
  explicit OpenAiCompatNarrator(const QString &baseUrl, const QString &apiKey,
                                const QString &model, QObject *parent = nullptr);
  QString narrate(const QString &prompt, QString *error = nullptr) override;
private:
  QString m_base, m_key, m_model;
};

QString postJsonSync(const QUrl &url, const QByteArray &body,
                     const QList<QPair<QByteArray,QByteArray>> &headers,
                     QByteArray *replyOut, QString *error, int timeoutMs = 120000);

// Strip secrets (api keys, bearer tokens) from text shown in the UI.
QString scrubSecrets(const QString &s);
