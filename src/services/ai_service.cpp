#include "ai_service.h"
#include "core/timeline.h"
#include <QRegularExpression>

namespace NS {

QString Narrator::buildPrompt(const Project &p, int fromPage, int toPage,
                              const QString &priorStory) {
  QStringList L;
  L << "You are writing the narration for an audiobook-style manga adaptation."
    << "The final audio is spoken by ONE calm narrator voice — you are writing"
    << "prose narration of the story, NOT dubbing the characters' dialogue."
    << "Report dialogue in the narrator's voice — never as character lines."
    << ""
    << "The dialogue text below comes from OCR on comic pages, so it often has"
    << "errors: run-together words, missing spaces, digits swapped for letters"
    << "(1 for I), and garbled punctuation. Infer the intended words and write"
    << "them correctly — never quote broken OCR text verbatim."
    << "";
  L << QString("Story: %1").arg(p.name);
  if (!p.synopsis.trimmed().isEmpty()) {
    L << "" << "SYNOPSIS (world premise — use for continuity on textless pages):"
      << p.synopsis.trimmed();
  }
  if (!p.characters.isEmpty()) {
    L << "Characters:";
    for (const auto &c : p.characters)
      L << QString("- %1%2").arg(c.name, c.role.isEmpty() ? "" : " (" + c.role + ")");
  }
  QString prior = priorStory.trimmed();
  if (!prior.isEmpty()) {
    if (prior.size() > 8000) prior = QStringLiteral("\u2026") + prior.right(8000);
    L << "" << "STORY SO FAR (already narrated in earlier episodes):" << prior
      << "" << "The pages below continue right after the story so far."
      << "Write a smooth continuation: do NOT recap, just keep telling the story.";
  }
  L << "" << "Each page lists what happens visually and the dialogue spoken on it."
    << "Paraphrase dialogue and actions rather than reading literally."
    << "Between pages emit exactly one separator line: [PAGE n]."
    << "Keep dry humor minimal; never break the fourth wall."
    << "";
  for (const auto &pg : p.pages) {
    if (pg.number < fromPage || pg.number > toPage) continue;
    L << QString("--- PAGE %1 ---").arg(pg.number);
    if (!pg.description.trimmed().isEmpty()) L << "Visual: " + pg.description.trimmed();
    if (!pg.cast.isEmpty()) L << "Visible: " + pg.cast.join(", ");
    for (const auto &b : pg.blocks)
      L << QString("  %1: %2").arg(b.speaker.isEmpty() ? "Text" : b.speaker, b.content);
    L << "";
  }
  L << QString("Write the narration now for pages %1-%2, with [PAGE n] separators.").arg(fromPage).arg(toPage);
  return L.join("\n");
}

QPair<QString, QVector<PageSegment>> Narrator::parseTagged(
    const QString &raw, int fromPage, int toPage) {
  static const QRegularExpression tag(R"(\[PAGE\s+(\d+)\])",
      QRegularExpression::CaseInsensitiveOption);
  QString clean; clean.reserve(raw.size());
  QVector<PageSegment> segs;
  int cur = fromPage, segStart = 0, pos = 0;
  auto it = tag.globalMatch(raw);
  int last = 0;
  auto flush = [&](const QString &chunk) {
    QString noTags = Timeline::stripPageMarkers(chunk);
    // collapse 3+ newlines, trim edges per segment
    noTags.replace(QRegularExpression("\n{3,}"), "\n\n");
    clean += noTags; pos += noTags.size();
  };
  while (it.hasNext()) {
    auto m = it.next();
    flush(raw.mid(last, m.capturedStart() - last));
    bool ok = false; int n = m.captured(1).toInt(&ok);
    if (ok && n >= fromPage && n <= toPage) {
      segs.append({cur, segStart, pos});
      cur = n; segStart = pos;
    }
    last = m.capturedEnd();
    // skip single newline right after tag line
    if (last < raw.size() && raw[last] == '\n') last++;
  }
  flush(raw.mid(last));
  segs.append({cur, segStart, pos});
  return {clean.trimmed(), segs};
}

int Narrator::largestFittingChunkEnd(const Project &p, int fromPage, int maxChars) {
  int used = 0;
  int end = fromPage - 1;
  for (const auto &pg : p.pages) {
    if (pg.number < fromPage) continue;
    int cost = pg.description.size() + 64;
    for (const auto &b : pg.blocks) cost += b.content.size() + 16;
    if (end >= fromPage && used + cost > maxChars) break;
    used += cost; end = pg.number;
  }
  return qMax(end, fromPage);
}

} // namespace NS
