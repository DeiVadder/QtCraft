#pragma once

#include "QcShowcaseArea.h"

// Every tree and column feature of the registry, grown as the world generator grows it, and in
// front of each the saplings that grow into it, planted and growing (decision E-220).
namespace QcShowcaseTreeArea {

QcShowcaseArea build(const QcShowcaseContext &context, QcBlockPos anchor);

} // namespace QcShowcaseTreeArea
