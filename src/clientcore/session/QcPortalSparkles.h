// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/session/QcEffectPuff.h"
#include "clientcore/session/QcNearbyBlocks.h"

/*! \brief The sparkles around the lit portals near the player (decision E-1022): the portal blocks
    (QcPortalShapes::portalOfBlock) within \c {radius} blocks, found again every \c {rescanTicks}
    (QcNearbyBlocks), and a few specks per block that float out of the portal, swirl and fade in a
    loop. Like the campfire smoke, they follow from the position and the time alone. */
class QcPortalSparkles
{
public:
    QcPortalSparkles();

    /*! Blocks around the eye within which portals sparkle. */
    static constexpr int radius = 24;
    /*! Ticks between two looks for the portal blocks. */
    static constexpr int rescanTicks = 10;
    /*! Specks around each portal block at a time. */
    static constexpr int specksPerBlock = 2;
    /*! Seconds a speck takes from the portal to where it fades. */
    static constexpr double speckSeconds = 2.0;
    /*! Portal blocks drawn with specks at most (the nearest). */
    static constexpr int maxBlocks = 48;

    /*! Looks for the portal blocks of \a world around \a eye again when \a tick is rescanTicks
        past the last look or the world changed. */
    void refresh(const QcClientWorld &world, const QcVec3 &eye, quint64 tick) { m_blocks.refresh(world, eye, tick); }
    /*! Returns the portal blocks found by the latest look, the nearest at most maxBlocks. */
    const QList<QcBlockPos> &blocks() const { return m_blocks.found(); }
    /*! Returns the specks of the portal block at \a pos at \a seconds of the animations' clock. */
    [[nodiscard]] static QList<QcEffectPuff> specksOf(QcBlockPos pos, double seconds);

private:
    QcNearbyBlocks m_blocks;
};
