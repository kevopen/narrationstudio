#include "tts_service.h"
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

QString scrubSecrets(const QString &s) {
  QString out = s;
  // ?key= / &key= query secrets (40+ char tokens)
  static const QRegularExpression keyQ("([?&]key=)[^&\\s\"']{8,}");
  out.replace(keyQ, "\\1***");
  // Bearer tokens
  static const QRegularExpression bearer("(Bearer\\s+)[A-Za-z0-9_\\-.~+/=]{8,}");
  out.replace(bearer, "\\1***");
  // x-goog-api-key header echoes in some stacks
  static const QRegularExpression gkey("([\"']?(?:x-goog-api-key|api[_-]?key)[\"']?\\s*[:=]\\s*[\"']?)[A-Za-z0-9_\\-.~+/=]{8,}");
  out.replace(gkey, "\\1***");
  return out;
}

QString postJsonSync(const QUrl &url, const QByteArray &body,
                     const QList<QPair<QByteArray,QByteArray>> &headers,
                     QByteArray *replyOut, QString *error, int timeoutMs) {
  QNetworkAccessManager nam;
  QNetworkRequest req(url);
  req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  for (auto &h : headers) req.setRawHeader(h.first, h.second);
  QNetworkReply *rep = nam.post(req, body);
  QEventLoop loop; QTimer t; t.setSingleShot(true);
  QObject::connect(&t, &QTimer::timeout, &loop, [&]{ if (rep->isRunning()) rep->abort(); });
  QObject::connect(rep, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  t.start(timeoutMs); loop.exec();
  if (rep->error() != QNetworkReply::NoError) {
    // The body usually carries the provider's real JSON error — surface it
    // instead of the bare "server replied" line. Google wraps it in an
    // array: [{"error": {"message": ...}}].
    QString detail;
    const QByteArray raw = rep->readAll();
    if (!raw.isEmpty()) {
      const QJsonDocument doc = QJsonDocument::fromJson(raw);
      const QJsonObject o = doc.isArray() ? doc.array().first().toObject()
                                          : doc.object();
      const QJsonObject eo = o["error"].toObject();
      detail = eo["message"].toString();
      if (detail.isEmpty()) detail = o["error"].toString();
      if (detail.isEmpty() && !o.isEmpty()) detail = QString::fromUtf8(raw).left(300);
    }
    QString msg = rep->errorString();
    if (!detail.isEmpty()) {
      msg = msg.trimmed();
      if (msg.endsWith(':')) msg.chop(1);
      msg += " - " + detail;
    }
    if (error) *error = scrubSecrets(msg);
    rep->deleteLater(); return {};
  }
  QByteArray data = rep->readAll();
  rep->deleteLater();
  if (replyOut) *replyOut = data;
  return QString::fromUtf8(data);
}

GeminiNarrator::GeminiNarrator(const QString &apiKey, QObject *parent)
  : INarrator(parent), m_key(apiKey) {}

QString GeminiNarrator::narrate(const QString &prompt, QString *error) {
  QUrl url(QString("https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:generateContent?key=%1").arg(m_key));
  QJsonObject part; part["text"] = prompt;
  QJsonObject content; content["parts"] = QJsonArray{part};
  QJsonObject body; body["contents"] = QJsonArray{content};
  QByteArray reply;
  postJsonSync(url, QJsonDocument(body).toJson(), {}, &reply, error);
  if (reply.isEmpty()) return {};
  QJsonObject o = QJsonDocument::fromJson(reply).object();
  // candidates[0].content.parts[*].text
  QString out;
  for (const auto &c : o["candidates"].toArray())
    for (const auto &p : c.toObject()["content"].toObject()["parts"].toArray())
      out += p.toObject()["text"].toString();
  if (out.isEmpty() && error && error->isEmpty()) *error = "empty Gemini response";
  return out;
}

OllamaNarrator::OllamaNarrator(const QString &baseUrl, const QString &model, QObject *parent)
  : INarrator(parent), m_base(baseUrl), m_model(model) {}

QString OllamaNarrator::narrate(const QString &prompt, QString *error) {
  QUrl url(m_base + "/api/generate");
  QJsonObject body; body["model"] = m_model; body["prompt"] = prompt; body["stream"] = false;
  QByteArray reply;
  postJsonSync(url, QJsonDocument(body).toJson(), {}, &reply, error, 300000);
  if (reply.isEmpty()) return {};
  return QJsonDocument::fromJson(reply).object()["response"].toString();
}

OpenAiCompatNarrator::OpenAiCompatNarrator(const QString &baseUrl, const QString &apiKey,
                                           const QString &model, QObject *parent)
  : INarrator(parent), m_base(baseUrl), m_key(apiKey), m_model(model) {}

QString OpenAiCompatNarrator::narrate(const QString &prompt, QString *error) {
  QUrl url(m_base + "/chat/completions");
  // All OpenAI-compatible endpoints (Groq/OpenRouter AND Google's OpenAI
  // surface) take Authorization: Bearer. Google rejects x-goog-api-key here
  // ("Missing or invalid Authorization header"), and the key must never sit
  // in the URL query — Qt echoes the URL inside error strings.
  QList<QPair<QByteArray,QByteArray>> headers;
  headers.append({"Authorization", ("Bearer " + m_key).toUtf8()});
  QJsonObject msg; msg["role"] = "user"; msg["content"] = prompt;
  QJsonObject body;
  body["model"] = m_model;
  body["messages"] = QJsonArray{msg};
  body["temperature"] = 0.7;
  QByteArray reply;
  postJsonSync(url, QJsonDocument(body).toJson(), headers, &reply, error, 300000);
  if (reply.isEmpty()) return {};
  const QJsonDocument rdoc = QJsonDocument::fromJson(reply);
  const QJsonObject o = rdoc.isArray() ? rdoc.array().first().toObject()
                                       : rdoc.object();
  if (o.contains("error")) {
    if (error) *error = scrubSecrets(o["error"].toObject()["message"].toString(o["error"].toString()));
    return {};
  }
  const QJsonArray choices = o["choices"].toArray();
  if (choices.isEmpty()) {
    if (error) *error = "empty model response";
    return {};
  }
  return choices.first().toObject()["message"].toObject()["content"].toString();
}
