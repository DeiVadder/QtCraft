#include "QcServerBlocks.h"

#include "QcBlockRules.h"
#include "QcBlockUpdateQueue.h"
#include "QcPhysics.h"
#include "QcServerSounds.h"
#include "QcServerState.h"

#include <algorithm>

using namespace Qt::StringLiterals;

QcServerBlocks::QcServerBlocks(QcServerState &state, QcServerSounds &sounds, QcBlockUpdateQueue &updates)
    : m_state(state)
    , m_sounds(sounds)
    , m_updates(updates)
{
}

void QcServerBlocks::setAndBroadcast(QcBlockPos pos, QcBlockId block)
{
    if (!m_state.world.setBlock(pos, block)) {
        return;
    }

    // A machine that is replaced by anything but another machine spills its contents.
    if (m_state.machines.find(pos) && !m_state.machines.machineOf(block)) {
        dropMachineContents(m_state.machines.remove(pos));
    }

    // So does a chest replaced by anything but another storage block; a storage block of another
    // size keeps what fits and spills the rest.
    reconcileContainer(pos);

    for (const auto &client : m_state.clients) {
        if (client->state == QcServerClient::State::Playing && client->sentChunks.contains(pos.chunk())) {
            client->connection->send(QcMsg::BlockUpdate{pos, block});
        }
    }

    m_updates.queueNeighbours(pos, block != qcAirBlockId);
}

void QcServerBlocks::reconcileContainer(QcBlockPos pos)
{
    if (!m_state.containers.find(pos)) {
        return;
    }

    const std::optional<QcBlockId> block = m_state.world.blockAt(pos);
    const int slotCount = block ? m_state.containers.slotsOf(*block) : 0;
    if (slotCount == 0) {
        dropStacks(m_state.containers.remove(pos).stacks, pos);
    } else {
        dropStacks(m_state.containers.resize(pos, slotCount), pos);
    }
}

bool QcServerBlocks::overlapsBody(const QList<QcAabb> &boxes) const
{
    const auto hits = [&boxes](const QcAabb &body) {
        return std::ranges::any_of(boxes, [&body](const QcAabb &box) { return body.intersects(box); });
    };

    for (const auto &client : m_state.clients) {
        if (client->state == QcServerClient::State::Playing && !client->dead
            && hits(QcPhysics::boxAt(client->motion.position, QcPhysics::playerWidth, QcPhysics::playerHeight))) {
            return true;
        }
    }

    const QList<QcMobDefinition> &definitions = m_state.content->mobs();
    for (const QcMobManager::Mob &mob : m_state.mobs.mobs()) {
        if (mob.type >= 0 && mob.type < definitions.size()
            && hits(QcPhysics::boxAt(mob.motion.position, definitions[mob.type].width, definitions[mob.type].height))) {
            return true;
        }
    }
    return false;
}

void QcServerBlocks::toggle(QcBlockPos pos, QcBlockId block)
{
    const QcContentRegistry &content = *m_state.content;
    const QString open = content.property(block, u"open"_s) == u"true"_s ? u"false"_s : u"true"_s;
    const QcBlockDefinition &definition = content.block(block);
    QList<std::pair<QcBlockPos, QcBlockId>> changes{{pos, content.withProperty(block, u"open"_s, open)}};
    if (definition.door) {
        const QcBlockPos other = QcBlockRules::otherDoorHalf(m_state.world, pos, block);
        const std::optional<QcBlockId> partner = m_state.world.blockAt(other);
        if (partner && content.isSameBlock(*partner, block)) {
            changes.append({other, content.withProperty(*partner, u"open"_s, open)});
        }
    }

    // Swinging shut (or open) must not trap anyone inside the new shape.
    QList<QcAabb> boxes;
    for (const auto &[at, state] : std::as_const(changes)) {
        const QcVec3 corner{double(at.x), double(at.y), double(at.z)};
        for (const QcAabb &box : content.state(state).collision) {
            boxes.append(box.moved(corner));
        }
    }

    if (overlapsBody(boxes)) {
        return;
    }

    for (const auto &[at, state] : std::as_const(changes)) {
        setAndBroadcast(at, state);
    }

    m_sounds.playBlock(definition, QcBlockSoundAction::Place, pos);
}

void QcServerBlocks::breakBlock(QcBlockPos pos, bool drops)
{
    const std::optional<QcBlockId> previous = m_state.world.blockAt(pos);
    if (!previous || *previous == qcAirBlockId) {
        return;
    }

    const QcContentRegistry &content = *m_state.content;
    const QcBlockDefinition &definition = content.block(*previous);
    // A slab broken out of a double slab drops both halves.
    const bool doubleSlab = content.property(*previous, u"type"_s) == u"double"_s;
    const bool pairedDoor = definition.door && QcBlockRules::hasDoorPartner(m_state.world, pos, *previous);

    setAndBroadcast(pos, qcAirBlockId);
    if (m_state.world.blockAt(pos) != qcAirBlockId) {
        return;
    }

    if (pairedDoor) {
        breakBlock(QcBlockRules::otherDoorHalf(m_state.world, pos, *previous), drops);
    }

    m_sounds.playBlock(definition, QcBlockSoundAction::Break, pos);

    // The upper half of a door drops nothing: the lower half carries the door item.
    const bool doorTop = definition.door && content.property(*previous, u"half"_s) == u"top"_s;
    if (drops && !doorTop) {
        dropBlockItems(definition, pos);
        if (doubleSlab) {
            dropBlockItems(definition, pos);
        }
    }

    m_state.plants.blockRemoved(pos, *previous, m_state.world);
}

void QcServerBlocks::dropBlockItems(const QcBlockDefinition &definition, QcBlockPos pos)
{
    const QcVec3 centre{pos.x + 0.5, pos.y + 0.5, pos.z + 0.5};
    for (const QcItemDrop &drop : definition.drops) {
        if (drop.chance < 1.0 && m_state.random.generateDouble() >= drop.chance) {
            continue;
        }
        if (const std::optional<QcItemId> item = m_state.content->itemId(drop.item)) {
            m_state.items.spawnScattered({*item, drop.count, 0}, centre, m_state.newEntityId());
        }
    }
}

void QcServerBlocks::dropStacks(const QList<QcItemStack> &stacks, QcBlockPos pos)
{
    const QcVec3 centre{pos.x + 0.5, pos.y + 0.5, pos.z + 0.5};
    for (const QcItemStack &stack : stacks) {
        if (!stack.isEmpty()) {
            m_state.items.spawnScattered(stack, centre, m_state.newEntityId());
        }
    }
}

void QcServerBlocks::dropMachineContents(const QcMachineManager::Machine &machine)
{
    const QcVec3 centre{machine.pos.x + 0.5, machine.pos.y + 0.5, machine.pos.z + 0.5};
    for (const QcItemStack &stack : machine.stacks) {
        if (!stack.isEmpty()) {
            m_state.items.spawnScattered(stack, centre, m_state.newEntityId());
        }
    }
}
