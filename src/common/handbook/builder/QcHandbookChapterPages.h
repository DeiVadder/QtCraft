#pragma once

#include "QcHandbookEntry.h"

#include <QList>

struct QcHandbookContext;

// Handbook chapters about playing (category "playing"), in content order: their sections with rule
// and key placeholders filled, and links to related content.
namespace QcHandbookChapterPages {

QList<QcHandbookEntry> build(const QcHandbookContext &context);

} // namespace QcHandbookChapterPages
