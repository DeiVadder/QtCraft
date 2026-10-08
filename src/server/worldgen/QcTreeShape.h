#pragma once

#include "QcBlockDefinition.h"
#include "QcBlockPos.h"
#include "QcWorldgenDefinitions.h"

#include <functional>

// Block layout of tree and column features, shared by world generation and growing saplings so
// that grown trees look like generated ones.
namespace QcTreeShape {

struct Params
{
    QcFeatureDefinition::Type type = QcFeatureDefinition::Type::Tree;
    QcBlockId trunk = qcAirBlockId;
    QcBlockId leaves = qcAirBlockId;
    int minHeight = 4;
    int maxHeight = 6;
    bool cone = false;
};

// Receives every block of the feature; `onlyIntoAir` marks blocks (leaves) that must not replace
// anything but air.
using Put = std::function<void(QcBlockPos pos, QcBlockId block, bool onlyIntoAir)>;

// Trunk height for a `random` value in [0, 1).
int height(const Params &params, double random);
// Emits the feature standing on `base` (the lowest trunk block). `seed` decides the ragged corners
// of round crowns.
void build(const Params &params, QcBlockPos base, double random, quint64 seed, const Put &put);

} // namespace QcTreeShape
