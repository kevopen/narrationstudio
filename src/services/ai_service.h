#pragma once
#include <QPair>
#include <QString>
#include <QVector>
#include "core/project.h"

// Narration prompt builder + [PAGE n] parser + chunking.
// Mirrors mangastudio/app/services/ai_service.py (build_narration_prompt,
// parse_tagged_narration, largest_fitting_chunk_end).
namespace NS {
struct PageSegment { int page = 0; int start = 0; int end = 0; };
struct Narrator {
  static QString buildPrompt(const Project &p, int fromPage, int toPage,
                             const QString &priorStory = {});
  // Splits raw LLM output into per-page prose; returns {cleanText, segments}.
  // Segments: (pageNumber, charStart, charEnd) into cleanText.
  static QPair<QString, QVector<PageSegment>> parseTagged(const QString &raw,
      int fromPage, int toPage);
  static int largestFittingChunkEnd(const Project &p, int fromPage, int maxChars);
};
} // namespace NS
