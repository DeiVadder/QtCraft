#pragma once

#include "QcAabb.h"
#include "QcContentRegistry.h"

#include <vector>

class QcServerWorld;

// Damage of the blocks a body touches (lava, fire, magma; `contactDamage` of the block data): the
// strongest of the loaded blocks a box overlaps. Only sections marked with the trait ContactDamage
// (QcSectionMarks) are looked at, so a world without such blocks costs nothing.
class QcContactDamage
{
public:
    explicit QcContactDamage(const QcContentRegistry &content);

    // Whether any loaded chunk holds a block with contact damage at all.
    bool isPossibleIn(const QcServerWorld &world) const;
    // The strongest contact damage of the loaded blocks `box` overlaps, 0 for none.
    double in(const QcServerWorld &world, const QcAabb &box) const;

private:
    // Contact damage of every block state, indexed by id.
    std::vector<double> m_damage;
};
