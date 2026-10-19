#pragma once

#include "common/core/QcBlockPos.h"

#include <optional>

struct QcCreature;

// Where a creature on a long walk goes first (decision E-423): a path search reaches only so far
// (QcPathPlanner), so a walk to a goal farther than the routes' reach goes in legs, each to a cell
// the routes name (a villager's: the roads of its village, QcVillagerRoads). A walker in water
// without a path from there first swims to the shore they name. The state is the creature's own
// (QcMobLegs::State); asking the routes costs, so the caller's budget limits it per tick.
namespace QcMobLegs {

// A leg counts as walked this close to its cell; a walker the routes had no leg for asks them again
// after this many ticks.
inline constexpr double arrived = 2.0;
inline constexpr int retryTicks = 40;

class Routes;
struct Budget;

// The cell to search a path to, and how many questions it asked the routes (the caller takes them
// from its budget).
struct Towards
{
    QcBlockPos cell;
    int asked = 0;
};

// The shore to swim to (none on dry ground or while a path leads on), and whether the routes were asked.
struct Way
{
    std::optional<QcBlockPos> shore;
    bool asked = false;
};

struct State
{
    // The cell it walks to first, and the ticks until it asks the routes again after they had none.
    std::optional<QcBlockPos> leg;
    int retryIn = 0;
    // In water without a path to its goal: the cell on land it gets out to first.
    std::optional<QcBlockPos> shore;

    // The cell to search a path to now: the shore while it must get out of the water, the target
    // when it lies within reach, else the leg towards it. Asks the routes only while `budget` allows.
    [[nodiscard]] Towards towards(const QcCreature &mob, QcBlockPos target, const Routes &routes, Budget budget);
    // The shore a walker in water swims to when it has no path from there; none while it has a path
    // or stands on dry ground (where it forgets the shore).
    [[nodiscard]] Way wayOutOfWater(const QcCreature &mob, const Routes &routes, Budget budget);
};

// Where legs come from.
class Routes
{
public:
    virtual ~Routes() = default;
    // Goals within this many cells (along both axes) are walked to directly.
    virtual int reach() const = 0;
    // The next leg from `from` towards `to`; none when the routes bring it no closer.
    virtual std::optional<QcBlockPos> nextLeg(QcBlockPos from, QcBlockPos to) const = 0;
    // A cell on land to swim to from `from`.
    virtual std::optional<QcBlockPos> wayOutOfWater(QcBlockPos from) const = 0;
};

// How many questions to the routes are left this tick.
struct Budget
{
    int used = 0;
    int limit = 0;

    bool spent() const { return used >= limit; }
    // Takes one question; false (nothing taken) when the budget is spent.
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
