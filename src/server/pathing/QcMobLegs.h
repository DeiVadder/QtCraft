// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"

#include <optional>

struct QcCreature;

/*! \brief Where a creature on a long walk goes first (decision E-423): a path search reaches only
    so far (QcPathPlanner), so a walk to a goal farther than the routes' reach goes in legs, each to
    a cell the routes name (a villager's: the roads of its village, QcVillagerRoads).

    A walker in water without a path from there first swims to the shore they name. The state is
    the creature's own (QcMobLegs::State); asking the routes costs, so the caller's budget limits
    it per tick. */
namespace QcMobLegs {

/*! A leg counts as walked this close to its cell; a walker the routes had no leg for asks them
    again after this many ticks. \qcshared */
inline constexpr double arrived = 2.0;
inline constexpr int retryTicks = 40;

class Routes;
struct Budget;

/*! \brief The cell to search a path to, and how many questions it asked the routes (the caller
    takes them from its budget). */
struct Towards
{
    /*! The cell to search a path to. */
    QcBlockPos cell;
    /*! The questions it asked the routes. */
    int asked = 0;
};

/*! \brief The shore to swim to (none on dry ground or while a path leads on), and whether the
    routes were asked. */
struct Way
{
    /*! The shore to swim to. */
    std::optional<QcBlockPos> shore;
    /*! Whether the routes were asked. */
    bool asked = false;
};

/*! \brief What a walker remembers of its way between ticks. */
struct State
{
    /*! The cell it walks to first, and the ticks until it asks the routes again after they had
        none. \qcshared */
    std::optional<QcBlockPos> leg;
    int retryIn = 0;
    /*! In water without a path to its goal: the cell on land it gets out to first. */
    std::optional<QcBlockPos> shore;

    /*! Returns the cell \a mob searches a path to now towards \a target: the shore while it must
        get out of the water, the target when it lies within reach, else the leg towards it.
        Asks \a routes only while \a budget allows. */
    [[nodiscard]] Towards towards(const QcCreature &mob, QcBlockPos target, const Routes &routes, Budget budget);
    /*! Returns the shore \a mob, a walker in water, swims to when it has no path from there (asks
        \a routes while \a budget allows); none while it has a path or stands on dry ground (where
        it forgets the shore). */
    [[nodiscard]] Way wayOutOfWater(const QcCreature &mob, const Routes &routes, Budget budget);
};

/*! \brief Where legs come from. */
class Routes
{
public:
    virtual ~Routes() = default;
    /*! Returns the reach: goals within this many cells (along both axes) are walked to directly. */
    virtual int reach() const = 0;
    /*! Returns the next leg from \a from towards \a to; none when the routes bring it no closer. */
    virtual std::optional<QcBlockPos> nextLeg(QcBlockPos from, QcBlockPos to) const = 0;
    /*! Returns a cell on land to swim to from \a from. */
    virtual std::optional<QcBlockPos> wayOutOfWater(QcBlockPos from) const = 0;
};

/*! \brief How many questions to the routes are left this tick. */
struct Budget
{
    /*! Questions taken this tick. */
    int used = 0;
    /*! Questions allowed this tick. */
    int limit = 0;

    /*! Returns whether the budget is spent. */
    bool spent() const { return used >= limit; }
    /*! Takes one question; returns false (nothing taken) when the budget is spent. */
    [[nodiscard]] bool take()
    {
        if (spent()) {
            return false;
        }
        ++used;
        return true;
    }
};

} // namespace QcMobLegs
