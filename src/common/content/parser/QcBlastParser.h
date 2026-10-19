#pragma once

#include "common/content/definitions/QcBlockDefinition.h"

struct QcJsonFieldReader;

// Parses the explosion fields of a block (decision E-600): "blastResistance" and "explosive".
namespace QcBlastParser {

void parseBlast(QcBlockDefinition &block, QcJsonFieldReader &reader);

} // namespace QcBlastParser
