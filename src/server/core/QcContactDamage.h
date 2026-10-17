#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcAabb.h"

#include <vector>

class QcServerWorld;

// Damage of the blocks a body touches (lava, fire, magma; `contactDamage` of the block data) and how
// long they set it alight (`setsAlight`, decision E-393): the strongest of the loaded blocks a box
// overlaps. Only sections marked with the trait ContactDamage
// (QcSectionMarks) are looked at, so a world without such blocks costs nothing.
class QcContactDamage
{
public:
    // What touching blocks does to a body: the strongest damage and the longest burning (ticks).
    struct Touch
    {
        double damage = 0.0;
        int alightTicks = 0;
    };

    explicit QcContactDamage(const QcContentRegistry &content);

    // Whether any loaded chunk holds a block with contact damage at all.
    bool isPossibleIn(const QcServerWorld &world) const;
    // The strongest contact damage of the loaded blocks `box` overlaps, 0 for none.
    double in(const QcServerWorld &world, const QcAabb &box) const;
    // The strongest damage and the longest burning of the loaded blocks `box` overlaps.
    Touch touching(const QcServerWorld &world, const QcAabb &box) const;

private:
    // Contact damage and burning of every block state, indexed by id.
    std::vector<Touch> m_touch;
};
