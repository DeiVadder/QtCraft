// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blockbehaviour/QcLiquidReaction.h"

#include <QList>
#include <QString>

/*! \brief How a liquid block flows (see QcLiquidFlow).

    A flowing liquid has the state properties \c {level} ("0" for a source, "1".."distance" for
    the flow that far from it) and optionally \c {falling} (fed from above). */
struct QcLiquidDefinition
{
    /*! Largest \c {distance}. */
    static constexpr int maxDistance = 15;
    /*! Largest \c {ticks}. */
    static constexpr int maxTicks = 1200;
    /*! Largest \c {dropSearch}. */
    static constexpr int maxDropSearch = 8;

    /*! Blocks a flow reaches sideways from its source; 0 for liquids that stay where they are. */
    int distance = 0;
    /*! Server ticks between two flow steps (water 5, lava 30). */
    int ticks = 5;
    /*! Two sources next to each other over solid ground (or a source) make a new source. */
    bool infinite = false;
    /*! Blocks searched sideways for the nearest drop: the liquid then spreads only towards the
        nearest drops (0, or no drop that near: to every side). Server side only. */
    int dropSearch = 4;
    /*! Factor on swimming speed inside the liquid. */
    double movementFactor = 1.0;
    /*! Item a scooping item (empty bucket) turns into when it takes a source; empty: cannot be
        taken. */
    QString bucket;
    /*! Item a bottling item (glass bottle) turns into on the liquid; the liquid stays (server
        side only). */
    QString bottle;
    /*! What the liquid turns into where it touches other liquids (\c {"reactions"}, at most 16).
        Server side only. */
    QList<QcLiquidReaction> reactions;
    /*! Played now and then where the liquid spreads (server side only). */
    QString sound;

    /*! Returns whether the liquid spreads from its source. */
    bool flows() const { return distance > 0; }
};
