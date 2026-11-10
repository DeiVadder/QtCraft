// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcAabb.h"

#include <vector>

class QcServerWorld;

/*! \brief Damage of the blocks a body touches (lava, fire, magma; \c {contactDamage} of the block
    data) and how long they set it alight (\c {setsAlight}, decision E-393): the strongest of the
    loaded blocks a box overlaps.

    Only sections marked with the trait ContactDamage (QcSectionMarks) are looked at, so a world
    without such blocks costs nothing. */
class QcContactDamage
{
public:
    /*! \brief What touching blocks does to a body: the strongest damage and the longest burning
        (ticks), and whether a block of them burns items. \qcfields */
    struct Touch
    {
        double damage = 0.0;
        int alightTicks = 0;
        /*! Whether a block of them burns items (not a campfire, decision E-772). */
        bool burnsItems = false;
        /*! The strongest damage that is not heat (QcBlockDefinition::contactFire): what a
            fire-proof body feels. */
        double coldDamage = 0.0;
    };

    explicit QcContactDamage(const QcContentRegistry &content);

    /*! Returns whether any loaded chunk of \a world holds a block with contact damage at all. */
    bool isPossibleIn(const QcServerWorld &world) const;
    /*! Returns the strongest contact damage of the loaded blocks of \a world that \a box
        overlaps, 0 for none. */
    double in(const QcServerWorld &world, const QcAabb &box) const;
    /*! Returns the strongest damage and the longest burning of the loaded blocks of \a world
        that \a box overlaps. */
    Touch touching(const QcServerWorld &world, const QcAabb &box) const;

private:
    // Contact damage and burning of every block state, indexed by id.
    std::vector<Touch> m_touch;
};
