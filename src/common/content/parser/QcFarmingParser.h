#pragma once

#include "QcBlockDefinition.h"

struct QcJsonFieldReader;

// Parses the farming fields of blocks (decisions E-289 and E-290): "crop" (stages, pace, light,
// fruit of stems) and "soil" (moisture, water reach, what it reverts to).
namespace QcFarmingParser {

void parseCrop(QcBlockDefinition &block, QcJsonFieldReader &cropReader, const QString &packNamespace);
void parseSoil(QcBlockDefinition &block, QcJsonFieldReader &soilReader, const QString &packNamespace);

} // namespace QcFarmingParser
