#pragma once

#include "QcLiquidReaction.h"

#include <QList>
#include <QString>

// How a liquid block flows (see QcLiquidFlow). A flowing liquid has the state properties `level`
// ("0" for a source, "1".."distance" for the flow that far from it) and optionally `falling`
// (fed from above).
struct QcLiquidDefinition
{
    static constexpr int maxDistance = 15;
    static constexpr int maxTicks = 1200;
    static constexpr int maxDropSearch = 8;

    // Blocks a flow reaches sideways from its source; 0 for liquids that stay where they are.
    int distance = 0;
    // Server ticks between two flow steps (water 5, lava 30).
    int ticks = 5;
    // Two sources next to each other over solid ground (or a source) make a new source.
    bool infinite = false;
    // Blocks searched sideways for the nearest drop: the liquid then spreads only towards the
    // nearest drops (0, or no drop that near: to every side). Server side only.
    int dropSearch = 4;
    // Factor on swimming speed inside the liquid.
    double movementFactor = 1.0;
    // Item a scooping item (empty bucket) turns into when it takes a source; empty: cannot be taken.
    QString bucket;
    // Item a bottling item (glass bottle) turns into on the liquid; the liquid stays (server side only).
    QString bottle;
    QList<QcLiquidReaction> reactions;
    // Played now and then where the liquid spreads (server side only).
    QString sound;

    bool flows() const { return distance > 0; }
};
