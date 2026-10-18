#include "server/mobs/QcInfestation.h"

#include "common/physics/QcPhysics.h"
#include "server/blocks/QcServerBlocks.h"
#include "server/core/QcServerState.h"

#include <QList>

namespace {

// 0, -1, 1, -2, 2, ... up to `reach`: the reference's search order, nearest first.
QList<int> nearestFirst(int reach)
{
    QList<int> offsets{0};
    for (int step = 1; step <= reach; ++step) {
        offsets.append(-step);
        offsets.append(step);
    }
    return offsets;
}

// The offsets a hurt creature searches, layer by layer, nearest first.
QList<QcBlockPos> wakeOrder()
{
    const QList<int> heights = nearestFirst(QcInfestation::wakeHeight);
    const QList<int> sideways = nearestFirst(QcInfestation::wakeReach);
    QList<QcBlockPos> order;
    order.reserve(heights.size() * sideways.size() * sideways.size());
    for (const int dy : heights) {
        for (const int dx : sideways) {
            for (const int dz : sideways) {
                order.append({dx, dy, dz});
            }
        }
    }
    return order;
}

} // namespace

QcInfestation::QcInfestation(QcServerState &state, QcServerBlocks &blocks)
    : m_state(state)
    , m_blocks(blocks)
{
}

quint32 QcInfestation::release(QcServerState &state, const QcBlockDefinition &definition, QcBlockPos pos)
{
    const std::optional<int> type = definition.infestsMob.isEmpty() ? std::nullopt : state.content().mobIndex(definition.infestsMob);
    if (!type) {
        return 0;
    }
    const QcVec3 feet{pos.x + 0.5, double(pos.y), pos.z + 0.5};
    return state.entities().creatures().spawn(*type, feet, state.entities().newId());
}

void QcInfestation::tick()
{
    // Copies: waking adds creatures and hiding removes them.
    QList<QcCreature> hurt;
    QList<QcCreature> idle;
    for (const QcCreature &mob : m_state.entities().creatures().mobs()) {
        if (mob.dormant || !hides(mob)) {
            continue;
        }
        if (mob.hurtThisTick && mob.health > 0.0) {
            hurt.append(mob);
        } else if (mob.mayGrief(m_state.info().mobGriefing) && mob.chasing == 0 && mob.motion.onGround && !mob.hurt.active() && m_state.random().generateDouble() < hideChance) {
            idle.append(mob);
        }
    }

    for (const QcCreature &mob : std::as_const(hurt)) {
        wake(mob);
    }
    for (const QcCreature &mob : std::as_const(idle)) {
        hide(mob, qcAllBlockFaces[m_state.random().bounded(quint32(qcBlockFaceCount))]);
    }
}

int QcInfestation::wake(const QcCreature &mob)
{
    static const QList<QcBlockPos> order = wakeOrder();
    const QcBlockPos centre = QcPhysics::blockContaining(mob.motion.position);
    const auto blocks = map().waking.constFind(mob.type);
    if (blocks == map().waking.constEnd()) {
        return 0;
    }
    const bool griefing = mob.mayGrief(m_state.info().mobGriefing);
    int woken = 0;
    for (const QcBlockPos &offset : order) {
        // Cells of chunks that are not loaded have no block and wake nothing.
        const QcBlockPos pos = centre.offset(offset.x, offset.y, offset.z);
        const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
        const auto base = block ? blocks->constFind(*block) : blocks->constEnd();
        if (base == blocks->constEnd()) {
            continue;
        }

        // Breaking it with drops lets the creature out (release); when the waking creature may not
        // change the world (QcCreature::mayGrief: game rule mobGriefing off, confined, no mind) the
        // base block comes back instead.
        if (griefing) {
            m_blocks.breakBlock(pos, true);
        } else {
            m_blocks.setAndBroadcast(pos, *base);
        }
        ++woken;
        if (m_state.random().generateDouble() < 0.5) {
            break;
        }
    }
    return woken;
}

bool QcInfestation::hide(const QcCreature &mob, QcBlockFace face)
{
    if (!mob.mayGrief(m_state.info().mobGriefing)) {
        return false;
    }
    const QcFaceOffset offset = qcFaceOffset(face);
    const double middle = m_state.content().mobs()[mob.type].height * 0.5;
    const QcBlockPos pos = QcPhysics::blockContaining(mob.motion.position + QcVec3{0.0, middle, 0.0}).offset(offset.dx, offset.dy, offset.dz);
    const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
    const auto infestedBlock = block ? map().hiding.constFind({*block, mob.type}) : map().hiding.constEnd();
    if (infestedBlock == map().hiding.constEnd()) {
        return false;
    }

    m_blocks.setAndBroadcast(pos, *infestedBlock);
    m_state.entities().creatures().remove(mob.id);
    return true;
}

const QcInfestation::Map &QcInfestation::map()
{
    if (!m_mapped) {
        const QcContentRegistry &content = m_state.content();
        for (const QcBlockDefinition &definition : content.blocks()) {
            const std::optional<QcBlockId> block = content.blockId(definition.name);
            const std::optional<QcBlockId> base = definition.infestsBlock.isEmpty() ? std::nullopt : content.blockId(definition.infestsBlock);
            const std::optional<int> kind = definition.infestsMob.isEmpty() ? std::nullopt : content.mobIndex(definition.infestsMob);
            if (block && base && kind) {
                m_map.hiding.insert({*base, *kind}, *block);
            }
        }
        // Every state of an infested block lets its creature out.
        for (qsizetype id = 0; id < content.stateCount(); ++id) {
            const QcBlockDefinition &definition = content.block(QcBlockId(id));
            const std::optional<int> kind = definition.infestsMob.isEmpty() ? std::nullopt : content.mobIndex(definition.infestsMob);
            const std::optional<QcBlockId> base = definition.infestsBlock.isEmpty() ? std::nullopt : content.blockId(definition.infestsBlock);
            if (kind) {
                m_map.waking[*kind].insert(QcBlockId(id), base.value_or(qcAirBlockId));
            }
        }
        m_mapped = true;
    }
    return m_map;
}

bool QcInfestation::hides(const QcCreature &mob) const
{
    return mob.type >= 0 && mob.type < m_state.content().mobs().size() && m_state.content().mobs()[mob.type].hidesInBlocks;
}
