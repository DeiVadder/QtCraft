#pragma once

#include "common/content/definitions/QcBlockDefinition.h"

#include <QDataStream>

// Wire format of one block definition in the content sync, with its resolved shape. Reading
// validates everything that later feeds physics, light and the mesher.
namespace QcBlockSerializer {

void writeBlock(QDataStream &stream, const QcBlockDefinition &block, const QcShapeDefinition &shape);
// Reads into `block`, which carries the read shape inline. False (and a corrupt stream) on bad data.
bool readBlock(QDataStream &stream, QcBlockDefinition &block);
// The fuel time range the parser allows.
bool plausibleFuel(double seconds);

} // namespace QcBlockSerializer
