#pragma once

#include "common/content/definitions/QcBlockDefinition.h"

struct QcJsonFieldReader;

// Parses the explosion fields of a block (decision E-600): "blastResistance" and "explosive".
namespace QcBlastParser {

// The explosion fields; `block` (read so far) decides whether the block may be explosive.
[[nodiscard]] QcBlastDefinition parseBlast(const QcBlockDefinition &block, QcJsonFieldReader &reader);

} // namespace QcBlastParser
