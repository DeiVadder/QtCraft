#pragma once

#include "QcShowcaseArea.h"

// Every village style of the registry (decision E-403), mod styles included: one row per style
// with its well, its lamp post and each of its buildings once, doors to a road in front, each
// labelled with its title and style.
namespace QcShowcaseVillageArea {

// Whether the content has a village style to show.
bool hasStyles(const QcContentRegistry &content);
QcShowcaseArea build(const QcShowcaseContext &context, QcBlockPos anchor);

} // namespace QcShowcaseVillageArea
