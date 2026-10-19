#include "server/guardians/QcCrumbling.h"

#include "common/physics/QcPhysics.h"
#include "server/blocks/QcLiquidFlow.h"
#include "server/blocks/QcServerBlocks.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcServerCombat.h"

#include <array>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// The direction a yaw faces along the nearer axis: 0 north, 1 east, 2 south, 3 west.
int quarterOf(double yaw)
{
    return int(std::lround(yaw / 90.0)) & 3;
}

// Whether a crumbled block may take the cell: air, a replaceable block or flowing liquid, never a
// liquid's source (liquids are replaceable too).
bool isOpen(const QcContentRegistry &content, std::optional<QcBlockId> here)
{
    if (!here) {
        return false;
    }
    const QcBlockDefinition &block = content.block(*here);
    if (block.liquid) {
        return !QcLiquidFlow::isSource(content, *here);
    }
    return *here == qcAirBlockId || block.replaceable;
}

} // namespace

QcCrumbling::QcCrumbling(QcServerState &state, QcServerSounds &sounds, QcServerBlocks &blocks)
    : m_state(state)
    , m_sounds(sounds)
    , m_blocks(blocks)
{
}

void QcCrumbling::tick()
{
    // Gathered first: crumbling removes creatures from the store.
    QList<quint32> crumbling;
    for (const QcCreature &mob : m_state.entities().creatures().mobs()) {
        const QcMobDefinition &definition = m_state.content().mobs()[mob.type];
        if (definition.crumbles && !mob.dormant && touchesSource(mob, definition, definition.crumbles->liquid)) {
            crumbling.append(mob.id);
        }
    }
    for (const quint32 id : std::as_const(crumbling)) {
        const QcCreature *mob = m_state.entities().creatures().find(id);
        if (!mob) {
            continue;
        }
        if (const std::optional<QcCrumbleDefinition> &rule = m_state.content().mobs()[mob->type].crumbles) {
            crumble(*mob, *rule);
        }
    }
}

bool QcCrumbling::touchesSource(const QcCreature &mob, const QcMobDefinition &definition, const QString &liquid) const
{
    const QcContentRegistry &content = m_state.content();
    const std::optional<QcBlockId> source = content.blockId(liquid);
    if (!source) {
        return false;
    }
    const QcVec3 &at = mob.motion.position;
    const double half = definition.width / 2.0 + touch;
    const QcBlockPos low = QcPhysics::blockContaining({at.x - half, at.y - touch, at.z - half});
    const QcBlockPos high = QcPhysics::blockContaining({at.x + half, at.y + definition.height, at.z + half});
    const int sizeX = high.x - low.x + 1;
    const int sizeZ = high.z - low.z + 1;
    const int cells = sizeX * sizeZ * (high.y - low.y + 1);
    for (int cell = 0; cell < cells; ++cell) {
        const QcBlockPos pos = low.offset(cell % sizeX, cell / (sizeX * sizeZ), cell / sizeX % sizeZ);
        const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
        if (block && content.isSameBlock(*block, *source) && QcLiquidFlow::isSource(content, *block)) {
            return true;
        }
    }
    return false;
}

QList<QcBlockPos> QcCrumbling::buildCells(QcBlockPos feet, double yaw)
{
    // The arms lie across the facing: east and west of a creature looking north or south.
    const bool alongX = quarterOf(yaw) % 2 == 0;
    const QcBlockPos middle = feet.offset(0, 1, 0);
    return {feet, middle, middle.offset(alongX ? -1 : 0, 0, alongX ? 0 : -1), middle.offset(alongX ? 1 : 0, 0, alongX ? 0 : 1),
            middle.offset(0, 1, 0)};
}

void QcCrumbling::crumble(const QcCreature &mob, const QcCrumbleDefinition &rule)
{
    const QcContentRegistry &content = m_state.content();
    const QcMobDefinition &definition = content.mobs()[mob.type];
    const QcBlockPos feet = QcPhysics::blockContaining(mob.motion.position + QcVec3{0.0, 0.01, 0.0});
    const double yaw = mob.yaw;
    const QcVec3 centre = mob.motion.position + QcVec3{0.0, definition.height * 0.5, 0.0};
    const std::optional<QcBlockId> body = content.blockId(rule.body);
    const std::optional<QcBlockId> head = content.blockId(rule.head);
    // One that may not change the world (QcCreature::mayGrief, decision E-510) leaves its build as items.
    const bool griefing = mob.mayGrief(m_state.info().mobGriefing);
    m_state.entities().creatures().remove(mob.id);
    ++m_crumbled;
    if (!rule.sound.isEmpty()) {
        m_sounds.play(rule.sound, centre);
    }

    // Bottom up, so a block falls onto the ones below it.
    const QList<QcBlockPos> cells = buildCells(feet, yaw);
    for (qsizetype i = 0; i < cells.size(); ++i) {
        const bool isHead = i == cells.size() - 1;
        const std::optional<QcBlockId> block = isHead ? head : body;
        if (block && griefing) {
            place(cells[i], isHead ? headFacing(*block, yaw) : *block);
        } else if (block) {
            drop(cells[i], *block);
        }
    }
}

void QcCrumbling::place(QcBlockPos cell, QcBlockId block)
{
    // Sand falls from there as usual and may fill a water hole below (decision E-593: the farm's
    // builder keeps the water out of the arms' way).
    if (isOpen(m_state.content(), m_state.world().blockAt(cell))) {
        m_blocks.setAndBroadcast(cell, block);
        return;
    }
    drop(cell, block);
}

void QcCrumbling::drop(QcBlockPos cell, QcBlockId block)
{
    const QcContentRegistry &content = m_state.content();
    const QString item = content.block(block).name;
    if (content.itemId(item)) {
        QcServerCombat::dropLoot(m_state, {QcItemDrop{.item = item, .count = 1}}, {cell.x + 0.5, cell.y + 0.5, cell.z + 0.5});
    }
}

QcBlockId QcCrumbling::headFacing(QcBlockId head, double yaw) const
{
    static const std::array<QString, 4> facings{u"north"_s, u"east"_s, u"south"_s, u"west"_s};
    const QcContentRegistry &content = m_state.content();
    if (content.property(head, u"facing"_s).isEmpty()) {
        return head;
    }
    return content.withProperty(head, u"facing"_s, facings[std::size_t(quarterOf(yaw))]);
}
