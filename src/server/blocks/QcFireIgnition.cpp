#include "server/blocks/QcFireIgnition.h"

#include "common/content/definitions/QcSoundKeys.h"
#include "server/blocks/QcServerFire.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcLineOfSight.h"
#include "server/gameplay/QcServerInventory.h"

#include <array>

namespace {

// What the dice of a block update with `sparks` decide.
enum Salt : quint32 {
    SparkSalt = 50,
    CellSalt = 60,
    LightningSalt = 80,
};

} // namespace

QcFireIgnition::QcFireIgnition(QcServerState &state, QcServerFire &fire, QcServerSounds &sounds, QcServerInventory &inventory)
    : m_state(state)
    , m_fire(fire)
    , m_sounds(sounds)
    , m_inventory(inventory)
    , m_table(state.content())
{
}

bool QcFireIgnition::use(QcServerClient &client, const QcMsg::UseItem &message)
{
    const QcItemDefinition *held = m_state.heldItem(client);
    if (!held || !held->ignites) {
        return false;
    }

    // An explosive clicked on is lit itself (decision E-601); anything else lights the cell in front.
    const bool explosive = !client.dead && QcLineOfSight::isWithinReach(client, message.target) && m_light && m_light(message.target);
    if (explosive) {
        m_sounds.play(QString::fromLatin1(QcSoundKeys::flintAndSteel), {message.target.x + 0.5, message.target.y + 0.5, message.target.z + 0.5});
        m_inventory.wearHeld(client);
        return true;
    }
    const QcBlockPos front = qcNeighbour(message.target, message.face);
    if (client.dead || !QcLineOfSight::isWithinReach(client, front) || !strike(front)) {
        return true;
    }
    m_inventory.wearHeld(client);
    return true;
}

bool QcFireIgnition::strike(QcBlockPos pos)
{
    const bool lit = (m_light && m_light(pos)) || m_fire.ignite(pos);
    if (!lit) {
        return false;
    }
    m_sounds.play(QString::fromLatin1(QcSoundKeys::flintAndSteel), {pos.x + 0.5, pos.y + 0.5, pos.z + 0.5});
    return true;
}

void QcFireIgnition::blockUpdated(QcBlockPos pos, QcBlockId block)
{
    // Lava lights fire only while fire spreads (game rule fireSpread, fireSpreadRadius above 0).
    const int sparks = m_table.of(block).sparks;
    if (sparks == 0 || !m_fire.spreads()) {
        return;
    }

    // Drawn from the seed, the position and the tick: the same world lights the same.
    const quint64 tick = m_state.clock().tick();
    const QcFireRules::Dice dice{static_cast<quint64>(m_state.config().seed), pos, static_cast<int>(tick % 0x7FFFFFFFu)};
    if (dice.roll(pos, SparkSalt, QcFireDefinition::maxChance) >= static_cast<quint32>(sparks)) {
        return;
    }

    // One of the air cells right next to it that touch a flammable block, picked by the dice.
    std::array<QcBlockPos, 6> cells{};
    quint32 count = 0;
    for (const QcBlockFace face : qcAllBlockFaces) {
        const QcBlockPos cell = qcNeighbour(pos, face);
        if (cell.isInsideWorldHeight() && m_state.world().blockAt(cell) == qcAirBlockId && QcFireRules::hasFuelNear(m_state.world(), m_table, cell)) {
            cells[count++] = cell;
        }
    }
    if (count > 0) {
        m_fire.ignite(cells[dice.roll(pos, CellSalt, count)]);
    }
}

int QcFireIgnition::lightning(QcBlockPos pos, int sparks)
{
    if (sparks <= 0 || !m_fire.spreads()) {
        return 0;
    }

    int lit = m_fire.ignite(pos) ? 1 : 0;
    // Drawn from the seed, the position and the tick: the same world lights the same.
    const quint64 tick = m_state.clock().tick();
    const QcFireRules::Dice dice{static_cast<quint64>(m_state.config().seed), pos, static_cast<int>(tick % 0x7FFFFFFFu)};
    for (int spark = 1; spark < sparks; ++spark) {
        const int cell = static_cast<int>(dice.roll(pos, LightningSalt + static_cast<quint32>(spark), 27));
        const QcBlockPos near = pos.offset(cell % 3 - 1, cell / 3 % 3 - 1, cell / 9 - 1);
        lit += m_fire.ignite(near) ? 1 : 0;
    }
    return lit;
}
