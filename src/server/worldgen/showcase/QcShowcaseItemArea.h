#pragma once

#include "QcShowcaseArea.h"

// Every item of the registry, one of each, in a row of chests (decision E-220); each chest has a
// label with the range of items it holds.
namespace QcShowcaseItemArea {

QcShowcaseArea build(const QcShowcaseContext &context, QcBlockPos anchor);

} // namespace QcShowcaseItemArea
