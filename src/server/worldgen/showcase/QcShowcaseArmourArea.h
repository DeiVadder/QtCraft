#pragma once

#include "QcShowcaseArea.h"

#include <QStringList>

// Every armour set of the registry on an armour stand (decision E-238), mod sets included, weakest
// first; each stand stands on a podium facing the visitors, with the set's name and armour points.
// Without armour in the content the area is empty (a single podium-less row).
namespace QcShowcaseArmourArea {

// The sets in the order the area shows them: by armour points, then by name.
QStringList sets(const QcContentRegistry &content);

QcShowcaseArea build(const QcShowcaseContext &context, QcBlockPos anchor);

} // namespace QcShowcaseArmourArea
