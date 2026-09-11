#pragma once
#include "engines.h"
#include <QNetworkAccessManager>
#include <QSet>
#include <QStringList>

// Vision describe via Gemini Flash (free tier, ~1500 RPD per key).
// Uses the native Gemini generateContent endpoint with inline_data for images.
// Supports multi-key rotation on quota exhaustion and model failover.
class GeminiDescribeEngine : public ICaptionEngine {
  Q_OBJECT
public:
  explicit GeminiDescribeEngine(const QStringList &keys,
                                const QString &model,
                                const QStringList &fallbackModels,
                                QObject *parent = nullptr);
  QString describe(const QString &imagePath, QString *error = nullptr) override;

  // Set prior-page context for narrative continuity (called by runDescribe).
  void setContext(const QString &context) { m_context = context; }
  void setSynopsis(const QString &s) { m_synopsis = s; }

private:
  QString postGemini(const QString &model, const QString &key,
                     const QJsonObject &body, QString *error);
  QStringList m_keys;
  int m_keyIdx = 0;
  QSet<int> m_deadKeys;
  QString m_model;
  QStringList m_fallbacks;
  QString m_context;
  QString m_synopsis;
  QNetworkAccessManager m_nam;
};
