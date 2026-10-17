#pragma once

#include "common/content/registry/QcContentRegistry.h"

// Turns block states with a structure (decision E-219): `turns` quarter turns clockwise seen from
// above (north becomes east). Any property value "north", "east", "south" or "west" turns along
// (facing, attach), the connection properties named after the four sides swap their values
// (fences, walls, panes) and "axis" swaps x and z on odd turns (logs). Other properties stay. A
// turned value the block does not have keeps the old one, with a warning once per block and value.
namespace QcStateRotation {

QString turnedDirection(const QString &direction, int turns);
QcBlockId turned(const QcContentRegistry &content, QcBlockId state, int turns);

} // namespace QcStateRotation
