// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/session/QcClientWorld.h"
#include "clientcore/session/QcEffectPuff.h"
#include "clientcore/session/QcNearbyBlocks.h"
#include "common/core/QcVec3.h"

#include <QList>


/*! \brief The smoke rising from the lit campfires near the player (decision E-772): the campfires
    (blocks whose campfire cooks) within \c {radius} blocks, found again every \c {rescanTicks}
    (QcNearbyBlocks), and a few puffs per campfire that rise, drift and shrink in a loop.

    The puffs follow from the campfire's position and the time alone, so nothing is kept per
    puff. */
class QcCampfireSmoke
{
public:
    /*! Blocks around the eye within which campfires smoke. */
    static constexpr int radius = 32;
    /*! Ticks between two looks for the campfires. */
    static constexpr int rescanTicks = 10;
    /*! Puffs in the air above each campfire at a time. */
    static constexpr int puffsPerCampfire = 5;
    /*! Seconds a puff takes from the fire to where it fades. */
    static constexpr double puffSeconds = 3.0;
    /*! Campfires drawn with smoke at most (the nearest). */
    static constexpr int maxCampfires = 64;

    /*! Looks for the campfires of \a world around \a eye again when \a tick is rescanTicks past
        the last look or the world changed. */
    void refresh(const QcClientWorld &world, const QcVec3 &eye, quint64 tick) { m_campfires.refresh(world, eye, tick); }
    /*! Returns the smoking campfires found by the latest look, the nearest at most maxCampfires. */
    const QList<QcBlockPos> &campfires() const { return m_campfires.found(); }
    /*! Returns the puffs of the campfire at \a pos at \a seconds of the animations' clock
        (QcWorldMirror::animationSeconds, decision E-962). */
    [[nodiscard]] static QList<QcEffectPuff> puffsOf(QcBlockPos pos, double seconds);
    /*! Returns a number in [0, 1) for loop \a round of puff \a index of the block at \a pos,
        stirred by \a salt: the same for the same arguments (also for QcPortalSparkles). */
    [[nodiscard]] static double unitOf(QcBlockPos pos, int index, qint64 round, quint32 salt);

private:
    QcNearbyBlocks m_campfires{{radius, rescanTicks, maxCampfires},
                               [](const QcContentRegistry &content, QcBlockId id) { return content.block(id).campfire.cooks(); }};
};
