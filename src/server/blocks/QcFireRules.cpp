#include "server/blocks/QcFireRules.h"

#include "common/core/QcBlockFace.h"

#include <algorithm>
#include <cstdlib>

namespace QcFireRules {

Table::Table(const QcContentRegistry &content)
{
    m_traits.resize(static_cast<std::size_t>(content.stateCount()));
    for (std::size_t id = 0; id < m_traits.size(); ++id) {
        const QcBlockDefinition &block = content.block(static_cast<QcBlockId>(id));
        Traits &traits = m_traits[id];
        traits.fire = block.fire.isFire;
        traits.eternal = block.fire.eternal;
        traits.solidTop = block.solid && content.state(static_cast<QcBlockId>(id)).fullFace[static_cast<std::size_t>(QcBlockFace::Up)];
        traits.ignite = static_cast<quint8>(block.fire.ignite);
        traits.burn = static_cast<quint8>(block.fire.burn);
        traits.sparks = static_cast<quint8>(block.fire.sparks);
    }
    m_defaultFire = content.defaultFire();
}

bool canStay(const QcBlockAccess &world, const Table &table, QcBlockPos pos)
{
    const std::optional<QcBlockId> below = world.blockAt(pos.offset(0, -1, 0));
    return (below && pos.y > 0 && table.of(*below).solidTop) || hasFuelNear(world, table, pos);
}

bool hasFuelNear(const QcBlockAccess &world, const Table &table, QcBlockPos pos)
{
    return std::ranges::any_of(qcAllBlockFaces, [&](QcBlockFace face) {
        const QcBlockPos near = qcNeighbour(pos, face);
        const std::optional<QcBlockId> block = near.isInsideWorldHeight() ? world.blockAt(near) : std::nullopt;
        return block && table.of(*block).flammable();
    });
}

int igniteNear(const QcBlockAccess &world, const Table &table, QcBlockPos pos)
{
    int ignite = 0;
    for (const QcBlockFace face : qcAllBlockFaces) {
        const QcBlockPos near = qcNeighbour(pos, face);
        if (const std::optional<QcBlockId> block = near.isInsideWorldHeight() ? world.blockAt(near) : std::nullopt) {
            ignite = std::max<int>(ignite, table.of(*block).ignite);
        }
    }
    return ignite;
}

bool withinReach(QcBlockPos origin, QcBlockPos pos, int reach)
{
    return std::abs(pos.x - origin.x) <= reach && std::abs(pos.y - origin.y) <= reach && std::abs(pos.z - origin.z) <= reach;
}

quint32 Dice::roll(QcBlockPos target, quint32 salt, quint32 bound) const
{
    // SplitMix64 over the inputs.
    quint64 h = seed ^ 0x9E3779B97F4A7C15ull;
    const auto word = [](int value) { return static_cast<quint64>(static_cast<quint32>(value)); };
    for (const quint64 value : {word(fire.x), word(fire.y), word(fire.z), word(age), word(target.x), word(target.y), word(target.z),
        static_cast<quint64>(salt)}) {
        h += value + 0x9E3779B97F4A7C15ull;
        h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9ull;
        h = (h ^ (h >> 27)) * 0x94D049BB133111EBull;
        h ^= h >> 31;
    }
    return bound == 0 ? 0 : static_cast<quint32>(h % bound);
}

quint64 Dice::nextStep(quint64 now) const
{
    return now + static_cast<quint64>(minStepTicks) + roll(fire, 1, stepTicksSpread);
}

} // namespace QcFireRules
