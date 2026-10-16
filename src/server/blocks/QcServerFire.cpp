#include "QcServerFire.h"

#include "QcServerBlocks.h"
#include "QcServerSounds.h"
#include "QcServerState.h"

#include <algorithm>

namespace {

// Chances of the reference game, with its age (QcFireRules::chanceAge): a block next to a fire
// burns with `burn` in 300 per step (250 above and below it), it becomes fire instead of air with 5
// in (age + 10); air next to flammable blocks catches with (ignite + 54) / (age + 30) in 100, less
// the higher above the fire it is.
constexpr quint32 burnSideDivisor = 300;
constexpr quint32 burnVerticalDivisor = 250;
constexpr quint32 becomesFire = 5;
constexpr int catchBonus = 54;
constexpr int catchAgeBonus = 30;
constexpr quint32 catchDivisor = 100;

// Rain puts out a fire it falls on with (rainBase + rainPerAge × age) in 100 per step, like the
// reference game with its age (QcFireRules::chanceAge).
constexpr quint32 rainBase = 20;
constexpr quint32 rainPerAge = 3;
constexpr quint32 rainDivisor = 100;

// What the dice decide, so that no two decisions of a step share a number.
enum Salt : quint32 {
    BurnSalt = 10,
    BecomesFireSalt = 20,
    CatchSalt = 30,
    ChildAgeSalt = 40,
    RainSalt = 70,
};

} // namespace

QcServerFire::QcServerFire(QcServerState &state, QcServerBlocks &blocks, QcServerSounds &sounds)
    : m_state(state)
    , m_blocks(blocks)
    , m_sounds(sounds)
    , m_table(state.content())
{
}

bool QcServerFire::spreads() const
{
    return m_state.info().fireSpread && m_state.config().limits.fireSpreadRadius > 0;
}

QcFireRules::Dice QcServerFire::dice(QcBlockPos fire, int age) const
{
    return {quint64(m_state.config().seed), fire, age};
}

int QcServerFire::reach() const
{
    return int(m_state.config().limits.fireSpreadRadius);
}

void QcServerFire::tick()
{
    const quint64 now = m_state.clock().tick();
    m_soundPlayed = false;
    putOutRefused();
    adoptUnplanned(now);

    QcFireQueue &fires = m_state.world().fires();
    for (qint64 budget = m_state.config().limits.fireStepsPerTick; budget > 0; --budget) {
        const std::optional<QcFireQueue::Fire> fire = fires.takeDue(now);
        if (!fire) {
            break;
        }
        ++m_numbers.steps;
        step(*fire, now);
    }
}

bool QcServerFire::ignite(QcBlockPos pos, std::optional<QcBlockId> fire)
{
    QcServerWorld &world = m_state.world();
    const std::optional<QcBlockId> block = fire ? fire : m_table.defaultFire();
    if (!block || !m_table.of(*block).fire || !pos.isInsideWorldHeight() || world.blockAt(pos) != qcAirBlockId
        || !QcFireRules::canStay(world, m_table, pos)) {
        return false;
    }
    const bool eternal = onEternalGround(pos);
    if (!eternal && !world.fires().hasRoom(pos.chunk())) {
        ++m_numbers.refused;
        return false;
    }

    const quint64 now = m_state.clock().tick();
    world.fires().add({pos, pos, 0, dice(pos, 0).nextStep(now), eternal});
    m_blocks.setAndBroadcast(pos, *block);
    ++m_numbers.lit;
    return true;
}

void QcServerFire::adoptUnplanned(quint64 now)
{
    QcFireQueue &fires = m_state.world().fires();
    if (fires.unplannedCount() == 0) {
        return;
    }

    // Fire set by a command or a player, or kept by a chunk without its entry: a new spark.
    const QList<QcBlockPos> unplanned = fires.takeUnplanned();
    for (const QcBlockPos &pos : unplanned) {
        const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
        if (!block || !m_table.of(*block).fire || fires.contains(pos)) {
            continue;
        }
        const bool eternal = onEternalGround(pos);
        if (!eternal && !fires.hasRoom(pos.chunk())) {
            ++m_numbers.refused;
            putOut(pos);
            continue;
        }
        fires.add({pos, pos, 0, dice(pos, 0).nextStep(now), eternal});
    }
}

void QcServerFire::putOutRefused()
{
    QcFireQueue &fires = m_state.world().fires();
    if (fires.refusedCount() == 0) {
        return;
    }

    // Fires that lost their entry for want of room go out; adopted, they would start a new budget.
    const QList<QcBlockPos> refused = fires.takeRefused();
    for (const QcBlockPos &pos : refused) {
        const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
        if (block && m_table.of(*block).fire && !fires.contains(pos)) {
            ++m_numbers.refused;
            putOut(pos);
        }
    }
}

bool QcServerFire::onEternalGround(QcBlockPos pos) const
{
    const std::optional<QcBlockId> below = pos.y > 0 ? m_state.world().blockAt(pos.offset(0, -1, 0)) : std::nullopt;
    return below && m_state.world().fires().isEternalGround(*below);
}

void QcServerFire::step(QcFireQueue::Fire fire, quint64 now)
{
    QcFireQueue &fires = m_state.world().fires();
    const QcServerWorld &world = m_state.world();
    const std::optional<QcBlockId> block = world.blockAt(fire.pos);
    if (!block || !m_table.of(*block).fire) {
        fires.drop(fire.pos);
        return;
    }
    if (!QcFireRules::canStay(world, m_table, fire.pos)) {
        putOut(fire.pos);
        return;
    }

    if (fire.eternal) {
        fire.due = dice(fire.pos, fire.age).nextStep(now);
        fires.plan(fire);
        return;
    }
    if (rainPutsOut(fire)) {
        ++m_numbers.rainedOut;
        putOut(fire.pos);
        return;
    }

    fire.age = quint8(std::min(fire.age + 1, QcFireQueue::maxAge));
    const bool fuel = QcFireRules::hasFuelNear(world, m_table, fire.pos);
    if (fire.age >= QcFireQueue::maxAge || (!fuel && fire.age > QcFireRules::ageWithoutFuel)) {
        putOut(fire.pos);
        return;
    }

    if (fuel && spreads()) {
        burnNeighbours(fire, *block, now);
        spreadAround(fire, *block, now);
    }
    // The fire keeps its entry while its block stands (burning what held it, it may have lost its
    // place; a block update puts it out then).
    fire.due = dice(fire.pos, fire.age).nextStep(now);
    fires.plan(fire);
}

bool QcServerFire::rainPutsOut(const QcFireQueue::Fire &fire) const
{
    if (!m_rainsOn || !m_rainsOn(fire.pos)) {
        return false;
    }

    const quint32 chance = rainBase + rainPerAge * quint32(QcFireRules::chanceAge(fire.age));
    return dice(fire.pos, fire.age).roll(fire.pos, RainSalt, rainDivisor) < chance;
}

void QcServerFire::burnNeighbours(const QcFireQueue::Fire &fire, QcBlockId block, quint64 now)
{
    const QcFireRules::Dice step = dice(fire.pos, fire.age);
    for (const QcBlockFace face : qcAllBlockFaces) {
        const QcBlockPos pos = qcNeighbour(fire.pos, face);
        const std::optional<QcBlockId> neighbour = pos.isInsideWorldHeight() ? m_state.world().blockAt(pos) : std::nullopt;
        if (!neighbour || m_table.of(*neighbour).burn == 0 || !QcFireRules::withinReach(fire.origin, pos, reach())) {
            continue;
        }

        const bool vertical = face == QcBlockFace::Up || face == QcBlockFace::Down;
        const quint32 divisor = vertical ? burnVerticalDivisor : burnSideDivisor;
        if (step.roll(pos, BurnSalt, divisor) >= m_table.of(*neighbour).burn) {
            continue;
        }

        const quint32 odds = quint32(QcFireRules::chanceAge(fire.age)) + 10;
        const QcBlockId burnt = *neighbour;
        if (step.roll(pos, BecomesFireSalt, odds) < becomesFire && light(pos, fire, block, now)) {
            ++m_numbers.burnt;
            m_state.plants().blockRemoved(pos, burnt, m_state.world());
        } else {
            burnAway(pos, burnt);
        }
    }
}

void QcServerFire::spreadAround(const QcFireQueue::Fire &fire, QcBlockId block, quint64 now)
{
    // The cells -1..1 east and south and -1..2 up of the fire, the fire's own cell left out.
    constexpr int cells = 3 * 3 * 4;
    for (int cell = 0; cell < cells; ++cell) {
        const int dx = cell % 3 - 1;
        const int dz = cell / 3 % 3 - 1;
        const int dy = cell / 9 - 1;
        const QcBlockPos pos = fire.pos.offset(dx, dy, dz);
        if ((dx != 0 || dy != 0 || dz != 0) && catches(fire, pos)) {
            light(pos, fire, block, now);
        }
    }
}

bool QcServerFire::catches(const QcFireQueue::Fire &fire, QcBlockPos pos) const
{
    const QcServerWorld &world = m_state.world();
    if (!pos.isInsideWorldHeight() || !QcFireRules::withinReach(fire.origin, pos, reach()) || world.blockAt(pos) != qcAirBlockId) {
        return false;
    }

    const int ignite = QcFireRules::igniteNear(world, m_table, pos);
    const int chance = ignite > 0 ? (ignite + catchBonus) / (QcFireRules::chanceAge(fire.age) + catchAgeBonus) : 0;
    const int above = pos.y - fire.pos.y;
    const quint32 divisor = catchDivisor + quint32(std::max(0, above - 1)) * catchDivisor;
    return chance > 0 && dice(fire.pos, fire.age).roll(pos, CatchSalt, divisor) <= quint32(chance);
}

bool QcServerFire::light(QcBlockPos pos, const QcFireQueue::Fire &parent, QcBlockId block, quint64 now)
{
    QcFireQueue &fires = m_state.world().fires();
    const bool eternal = onEternalGround(pos);
    // Fire does not spread into the rain.
    if (!eternal && m_rainsOn && m_rainsOn(pos)) {
        return false;
    }
    if (!eternal && !fires.hasRoom(pos.chunk())) {
        ++m_numbers.refused;
        return false;
    }

    const int older = dice(parent.pos, parent.age).roll(pos, ChildAgeSalt, 5) == 4 ? QcFireRules::stepsPerChanceAge : 0;
    const auto age = quint8(std::min(parent.age + older, QcFireQueue::maxAge));
    fires.add({pos, parent.origin, age, dice(pos, age).nextStep(now), eternal});
    m_blocks.setAndBroadcast(pos, block);
    ++m_numbers.lit;
    return true;
}

void QcServerFire::burnAway(QcBlockPos pos, QcBlockId burnt)
{
    m_blocks.setAndBroadcast(pos, qcAirBlockId);
    if (m_state.world().blockAt(pos) != qcAirBlockId) {
        return;
    }
    ++m_numbers.burnt;
    m_state.plants().blockRemoved(pos, burnt, m_state.world());
    if (!m_soundPlayed) {
        m_soundPlayed = true;
        m_sounds.playBlock(m_state.content().block(burnt), QcBlockSoundAction::Break, pos);
    }
}

void QcServerFire::putOut(QcBlockPos pos)
{
    m_blocks.setAndBroadcast(pos, qcAirBlockId);
    ++m_numbers.putOut;
}
