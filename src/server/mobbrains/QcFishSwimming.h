// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcCreatureKindId.h"
#include "common/core/QcVec3.h"
#include "common/physics/QcPhysics.h"
#include "server/pathing/QcSwimRoutes.h"

#include <QHash>
#include <QList>
#include <QRandomGenerator>

#include <span>
#include <utility>
#include <vector>

class QcBlockAccess;
class QcContentRegistry;
struct QcAquaticDefinition;
struct QcPulseDefinition;
struct QcCreature;
struct QcMobDefinition;

/*! \brief How water creatures move (fish, decision E-502), part of QcMobBehaviour: in water they
    swim without gravity along routes to goals where their body has room (QcSwimRoutes, a fleeing
    one away from danger), rest a moment at each goal and keep their whole body in the water
    (QcSwimSpace), steered also towards their school (cohesion, alignment and separation among the
    nearest creatures of their kind, \c {schooling}); a creature that swims in pulses (the squid,
    QcMobDefinition::pulse, decision E-974) pushes at the start of each pulse, glides between
    and tilts its body along its way (QcCreature::tilt); one without a mind (QcCreature::noAi)
    holds its place;
    out of water they fall, flop about and count their dry ticks (QcCreature::dryTicks), which
    QcBodyBurning turns into suffocation.

    The neighbours come from a grid of each kind built once a tick, its cells as large as the
    kind's school radius, so a member looks into 27 cells; the grid keeps its storage from tick
    to tick. */
class QcFishSwimming
{
public:
    /*! Smallest side of a cell of the neighbour grid, in metres (the school radius may be 0). */
    static constexpr double minCellSize = 1.0;
    /*! Chance per tick that a fish on dry ground flops. */
    static constexpr double flopChance = 0.1;
    /*! Seconds a creature rests at a goal, at most. */
    static constexpr double restMaxSeconds = 1.0;
    /*! Seconds a creature without room to swim waits before it looks again, at least and at
        most. \qcshared */
    static constexpr double waitMinSeconds = 1.0;
    static constexpr double waitMaxSeconds = 4.0;
    /*! Seconds a route may take besides its length at swimming speed, and the factor on that
        time, before the creature gives it up (stuck in its school or behind a new wall).
        \qcshared */
    static constexpr double routeSpareSeconds = 2.0;
    static constexpr double routeTimeFactor = 2.0;
    /*! Within this distance of its goal, in metres, a creature slows down. */
    static constexpr double slowDistance = 0.6;
    /*! Metres a creature tests its wish ahead for room in the water (QcSwimSpace::keepInside). */
    static constexpr double wishReach = 0.25;
    /*! Weight of its own route for a fish with a school around it. */
    static constexpr double schoolWander = 0.4;
    /*! A creature that swims in pulses (QcMobDefinition::pulse, decision E-974): the rates per
        second at which its velocity nears the push and slows while it glides, and its mean
        speed as a share of the push's (for the time a route may take). \qcshared */
    static constexpr double thrustRate = 6.0;
    static constexpr double glideRate = 1.2;
    static constexpr double pulseTravel = 0.5;
    /*! Above this speed in m/s a pulsing swimmer tilts along its way at \c {tiltRate} per
        second; slower it rights itself at \c {rightingRate}. \qcshared */
    static constexpr double tiltSpeed = 0.3;
    static constexpr double tiltRate = 4.0;
    static constexpr double rightingRate = 0.5;
    /*! Rate per second at which the water stops a creature without a mind (QcCreature::noAi). */
    static constexpr double driftRate = 1.0;

    explicit QcFishSwimming(QRandomGenerator &random);

    /*! Files the awake water creatures of \a mobs (kinds from \a content) of this tick into the
        neighbour grid. */
    void startTick(std::span<const QcCreature> mobs, const QcContentRegistry &content);
    /*! One tick of \a dt seconds of the water creature at \a index of \a mobs, of the kind
        \a definition, in \a world. */
    void swim(qsizetype index, std::span<QcCreature> mobs, const QcMobDefinition &definition, const QcBlockAccess &world, double dt);

    /*! Returns the steering a school adds to one member at \a position moving with \c {velocity}:
        towards the centre of the \a others (cohesion), along their mean heading (alignment) and
        away from those closer than separationDistance (separation), each weighted by \a aquatic.
        Pure. */
    static QcVec3 schooling(const QcVec3 &position, const QList<const QcCreature *> &others, const QcAquaticDefinition &aquatic);
    /*! Returns whether the cell of \a world at \a position holds water (not lava). */
    static bool inWater(const QcBlockAccess &world, const QcVec3 &position);

private:
    // The velocity a swimmer heads for and the rate per second at which it nears it.
    struct Drive
    {
        QcVec3 target;
        double rate = 0.0;
    };

    // The drive of a creature that swims in `pulse`s towards `wish` at `speed` over `dt` seconds:
    // a push at the start of each pulse, else a glide that slows down. Advances its pulse clock.
    static Drive pulseDrive(QcCreature &mob, const QcPulseDefinition &pulse, const QcVec3 &wish, double speed, double dt);
    // Tilts the body of a pulsing swimmer along its velocity (QcCreature::tilt).
    static void tiltAlong(QcCreature &mob, double dt);
    // A creature of the grid within the school radius and its squared distance.
    struct Candidate
    {
        double distanceSquared = 0.0;
        const QcCreature *mob = nullptr;
    };
    // A cell of the neighbour grid: the kind and its position in cells of that kind's size.
    using CellKey = std::pair<QcCreatureKindId, qint64>;

    // The nearest creatures of the same kind within the school radius of `mobs[index]`, at most
    // `neighbours` of them; valid until the next call.
    const QList<const QcCreature *> &neighboursOf(std::span<const QcCreature> mobs, qsizetype index, const QcAquaticDefinition &aquatic);
    // Adds the creatures of the grid cell `cell` that school with `mobs[index]` to m_candidates.
    void collectFrom(std::span<const QcCreature> mobs, qsizetype index, CellKey cell, double radius);
    void swimInWater(QcCreature &mob, const QcMobDefinition &definition, const QcVec3 &school, const QcBlockAccess &world, double dt);
    // Plans a new route when the old one is done or given up, or when the creature starts or
    // stops fleeing; `speed` is its swimming speed.
    void keepRoute(QcCreature &mob, QcPhysics::BodySize body, const QcBlockAccess &world, double speed);
    // The direction along the route (length up to 1, shorter close to the goal); zero while it
    // rests. A finished route starts the rest.
    QcVec3 routeWish(QcCreature &mob, QcPhysics::BodySize body, const QcBlockAccess &world);
    // One tick in water of a creature without a mind (QcCreature::noAi): it swims nowhere, the
    // water stops it and a pulsing swimmer rights itself.
    static void drift(QcCreature &mob, const QcMobDefinition &definition, const QcBlockAccess &world, double dt);
    // Moves the body of `mob` by its velocity over `dt` seconds, stopped by blocks, and turns it
    // along its way.
    static void move(QcCreature &mob, QcPhysics::BodySize body, const QcBlockAccess &world, double dt);
    void flopOnLand(QcCreature &mob, const QcMobDefinition &definition, const QcBlockAccess &world, double dt);
    // Keeps the wish of a body partly out of the water from leading farther out, tested at its
    // `centre`: no rising above the surface, no swimming into a shore.
    static QcVec3 keepCentreInWater(const QcVec3 &wish, const QcVec3 &centre, const QcBlockAccess &world);

    static qint64 cellKey(int x, int y, int z);
    static double cellSizeOf(const QcAquaticDefinition &aquatic);
    static CellKey cellOf(QcCreatureKindId type, const QcVec3 &position, double size);

    QRandomGenerator &m_random;
    QcSwimRoutes m_routes;
    // The grid as a counting sort: the creatures of cell c are m_members[m_cellStart[c]] up to
    // m_members[m_cellStart[c + 1]], c from m_cellIndex.
    // NOLINT(qc-member-access): the grid's index, rebuilt each tick by startTick.
    QHash<CellKey, qsizetype> m_cellIndex;
    // NOLINT(qc-member-access): scratch of startTick, kept for its storage.
    std::vector<qsizetype> m_cellOfMob;
    // NOLINT(qc-member-access): the grid's runs, rebuilt each tick by startTick.
    std::vector<qsizetype> m_cellStart;
    // NOLINT(qc-member-access): the grid's runs, rebuilt each tick by startTick.
    std::vector<qsizetype> m_members;
    // NOLINT(qc-member-access): scratch of neighboursOf, kept for its storage.
    std::vector<Candidate> m_candidates;
    // NOLINT(qc-member-access): the result of neighboursOf, kept for its storage.
    QList<const QcCreature *> m_found;
};
