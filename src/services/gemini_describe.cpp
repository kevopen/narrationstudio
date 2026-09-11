#include "gemini_describe.h"
#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QUrl>
#include <QUuid>
#include <stdexcept>

GeminiDescribeEngine::GeminiDescribeEngine(const QStringList &keys,
                                           const QString &model,
                                           const QStringList &fallbackModels,
                                           QObject *parent)
  : ICaptionEngine(parent), m_keys(keys), m_model(model),
    m_fallbacks(fallbackModels) {}

static QByteArray imageToBase64(const QString &path, QString *error) {
  QImageReader rd(path);
  if (!rd.canRead()) { if (error) *error = "Cannot read image: " + path; return {}; }
  QImage img = rd.read();
  if (img.isNull()) { if (error) *error = "Decode failed: " + rd.errorString(); return {}; }
  if (img.width() > 768 || img.height() > 768)
    img = img.scaled(768, 768, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  QByteArray ba;
  QBuffer buf(&ba);
  buf.open(QIODevice::WriteOnly);
  img.save(&buf, "JPEG", 85);
  return ba;
}

QString GeminiDescribeEngine::postGemini(const QString &model, const QString &key,
                                         const QJsonObject &body, QString *error) {
  const QString urlStr = QString(
    "https://generativelanguage.googleapis.com/v1beta/models/%1:generateContent")
    .arg(model);
  QUrl urlObj(urlStr);
  QNetworkRequest req(urlObj);
  req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  req.setRawHeader("x-goog-api-key", key.toUtf8());
  QNetworkReply *reply = m_nam.post(req, QJsonDocument(body).toJson());
  QEventLoop loop;
  connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  loop.exec();
  if (reply->error() != QNetworkReply::NoError) {
    QString msg;
    if (reply->error() == QNetworkReply::ContentNotFoundError)
      msg = "model not found";
    else if (reply->error() == QNetworkReply::TooManyRedirectsError
             || reply->error() == QNetworkReply::ContentOperationNotPermittedError)
      msg = "429 rate limit";
    else {
      QByteArray bodyData = reply->readAll();
      QJsonDocument d = QJsonDocument::fromJson(bodyData);
      msg = d["error"]["message"].toString(bodyData);
    }
    if (error) *error = msg;
    reply->deleteLater();
    return {};
  }
  QByteArray data = reply->readAll();
  reply->deleteLater();
  QJsonDocument doc = QJsonDocument::fromJson(data);
  QJsonObject root = doc.object();
  QString text;
  const QJsonArray candidates = root["candidates"].toArray();
  for (int i = 0; i < candidates.size(); ++i) {
    const QJsonObject c = candidates[i].toObject();
    const QJsonArray parts = c["content"]["parts"].toArray();
    for (int j = 0; j < parts.size(); ++j) {
      text += parts[j].toObject()["text"].toString();
    }
  }
  return text.trimmed();
}

QString GeminiDescribeEngine::describe(const QString &imagePath, QString *error) {
  QByteArray imgB64 = imageToBase64(imagePath, error);
  if (imgB64.isEmpty()) return {};

  QStringList prompt;
  prompt << "You are describing manga pages for an audiobook narration.";
  prompt << "Describe this page's scene concisely: who is present, what they"
            " are doing, the setting, mood, and time of day.";
  if (!m_synopsis.trimmed().isEmpty())
    prompt << "\nStory synopsis: " + m_synopsis.trimmed();
  if (!m_context.trimmed().isEmpty())
    prompt << "\nPrevious pages context:\n" + m_context.trimmed();
  prompt << "\nWrite as a narrative description suitable for a narrator to"
            " read aloud. Focus on visual details and emotional tone."
            " One paragraph, 2-4 sentences.";

  QJsonObject textPart;
  textPart["text"] = prompt.join("\n");
  QJsonObject imgPart;
  QJsonObject inlineData;
  inlineData["mime_type"] = "image/jpeg";
  inlineData["data"] = QString::fromLatin1(imgB64);
  imgPart["inline_data"] = inlineData;
  QJsonArray parts;
  parts.append(textPart);
  parts.append(imgPart);
  QJsonObject userContent;
  userContent["parts"] = parts;
  userContent["role"] = "user";
  QJsonArray contents;
  contents.append(userContent);
  QJsonObject genConfig;
  genConfig["maxOutputTokens"] = 200;
  genConfig["temperature"] = 0.3;
  QJsonObject body;
  body["contents"] = contents;
  body["generationConfig"] = genConfig;

  QStringList allModels;
  allModels << m_model;
  allModels.append(m_fallbacks);

  for (const QString &model : allModels) {
    for (int attempt = 0; attempt < m_keys.size(); ++attempt) {
      int ki = -1;
      for (int k = 0; k < m_keys.size(); ++k) {
        int idx = (m_keyIdx + k) % m_keys.size();
        if (!m_deadKeys.contains(idx)) { ki = idx; break; }
      }
      if (ki < 0) break;
      m_keyIdx = ki;

      QString err;
      QString result = postGemini(model, m_keys[ki], body, &err);
      if (!result.isEmpty()) return result;

      if (err.contains("429") || err.contains("rate limit")
          || err.contains("quota") || err.contains("exhausted")
          || err.contains("RESOURCE_EXHAUSTED")) {
        m_deadKeys.insert(ki);
        continue;
      }
      if (err.contains("model not found") || err.contains("NOT_FOUND"))
        break;
    }
  }

  if (error) *error = "All Gemini describe models/keys exhausted";
  return {};
}
