#pragma once

#include "QcBlockAccess.h"
#include "QcContentRegistry.h"

#include <optional>
#include <vector>

// The rules of fire (decision E-392), shared by QcServerFire and the tests: where fire may stand,
// how readily it catches on near flammable blocks and its deterministic dice.
namespace QcFireRules {

// What fire needs to know about a block state, looked up once per content.
struct Traits
{
    bool fire = false;
    // Fire on top of it never goes out and never spreads.
    bool eternal = false;
    // A solid block with a full top face: fire may stand on it without fuel next to it.
    bool solidTop = false;
    quint8 ignite = 0;
    quint8 burn = 0;
    // Chance in 100 that a block update of it lights a flammable neighbourhood (lava, decision E-393).
    quint8 sparks = 0;

    bool flammable() const { return ignite > 0 || burn > 0; }
};

class Table
{
public:
    explicit Table(const QcContentRegistry &content);

    const Traits &of(QcBlockId block) const
    {
        static const Traits none;
        return std::size_t(block) < m_traits.size() ? m_traits[std::size_t(block)] : none;
    }
    // The fire block a spark lights (QcContentRegistry::defaultFire).
    std::optional<QcBlockId> defaultFire() const { return m_defaultFire; }

private:
    std::vector<Traits> m_traits;
    std::optional<QcBlockId> m_defaultFire;
};

// Ticks between two steps of a fire: 30 to 39, like the reference game.
inline constexpr int minStepTicks = 30;
inline constexpr int stepTicksSpread = 10;
// Steps a fire takes for one unit of the reference game's age (which grows by one in about three steps there).
inline constexpr int stepsPerChanceAge = 3;
// A fire without a flammable block next to it goes out once older than this (the reference's 3).
inline constexpr int ageWithoutFuel = 3 * stepsPerChanceAge;

// The reference game's age (0 to 15) that the chances take, of a fire's age in steps.
constexpr int chanceAge(int age)
{
    return age / stepsPerChanceAge;
}

// Whether fire may stand at `pos`: a solid top face below or a flammable block next to it. Cells
// in chunks that are not loaded count as neither.
bool canStay(const QcBlockAccess &world, const Table &table, QcBlockPos pos);
// Whether a loaded block next to `pos` is flammable.
bool hasFuelNear(const QcBlockAccess &world, const Table &table, QcBlockPos pos);
// The highest `ignite` of the loaded blocks next to `pos`; 0 when none of them burns.
int igniteNear(const QcBlockAccess &world, const Table &table, QcBlockPos pos);
// Whether `pos` lies within `reach` blocks of `origin` along every axis.
bool withinReach(QcBlockPos origin, QcBlockPos pos, int reach);
// The dice of one step of a fire: drawn from the world's seed, the fire's position and its age, not
// from the tick, so the same in every run, independent of the order fires take their steps in, and
// the same after the fire's chunk was away for a while (decision E-374).
struct Dice
{
    quint64 seed = 0;
    QcBlockPos fire;
    int age = 0;

    // A number in [0, bound) for a decision (`salt`) about the cell `target`.
    quint32 roll(QcBlockPos target, quint32 salt, quint32 bound) const;
    // The tick of the fire's next step after a step at `now`.
    quint64 nextStep(quint64 now) const;
};

} // namespace QcFireRules
