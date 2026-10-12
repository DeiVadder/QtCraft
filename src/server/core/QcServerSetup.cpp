#include "QcServerSetup.h"

#include "QcBlockUpdateQueue.h"
#include "QcServerState.h"
#include "QcStandProperties.h"

#include <QLoggingCategory>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

Q_LOGGING_CATEGORY(lcSetup, "qtcraft.server")

QcBlockId blockAt(QcServerState &state, QcBlockPos pos)
{
    const std::shared_ptr<const QcChunk> chunk = state.world.loadChunk(pos.chunk());
    return chunk->block(pos.localX(), pos.y, pos.localZ());
}

QList<QcItemStack> stacksOf(const QcServerState &state, const QList<QcStructureDefinition::Stack> &items)
{
    QList<QcItemStack> stacks;
    for (const QcStructureDefinition::Stack &item : items) {
        const std::optional<QcItemId> id = state.content->itemId(item.item);
        if (!id) {
            qCWarning(lcSetup) << "World setup: unknown item" << item.item;
            continue;
        }
        stacks.append({*id, std::min(item.count, state.content->item(*id).maxStack), 0});
    }
    return stacks;
}

// A chest gets the stacks in its slotCount, a machine as input, fuel and output.
void fill(QcServerState &state, const QcWorldSetup::Contents &contents)
{
    const QcBlockId block = blockAt(state, contents.at);
    const QList<QcItemStack> stacks = stacksOf(state, contents.items);
    const int slotCount = state.containers.slotsOf(block);
    if (slotCount > 0 && state.containers.add(contents.at, slotCount)) {
        QcContainerManager::Container *container = state.containers.find(contents.at);
        for (qsizetype i = 0; i < stacks.size() && i < container->stacks.size(); ++i) {
            container->stacks[i] = stacks[i];
        }
        return;
    }

    if (state.machines.machineOf(block) && state.machines.add(contents.at)) {
        QcMachineManager::Machine *machine = state.machines.find(contents.at);
        for (qsizetype i = 0; i < stacks.size() && i < QcMachineManager::slotCount; ++i) {
            machine->stacks[std::size_t(i)] = stacks[i];
        }
        return;
    }
    qCWarning(lcSetup) << "World setup: no chest or machine for contents at" << contents.at.x << contents.at.y << contents.at.z;
}

// An armour stand of a structure; properties that do not apply are reported and left out.
void placeStand(QcServerState &state, const QcWorldSetup::Stand &setup)
{
    QcArmourStandManager::Stand stand;
    stand.id = state.newEntityId();
    stand.position = setup.at;
    stand.yaw = qcNormalizedAngle(setup.yaw);
    stand.item = QcStandProperties::standItem(*state.content);
    for (const QString &property : setup.properties) {
        if (!QcStandProperties::applyLine(*state.content, stand, property)) {
            qCWarning(lcSetup) << "World setup: armour stand property" << property << "does not apply";
        }
    }
    if (!state.stands.add(stand)) {
        qCWarning(lcSetup) << "World setup: too many armour stands";
    }
}

} // namespace

namespace QcServerSetup {

void apply(QcServerState &state, QcBlockUpdateQueue &updates, const QcWorldSetup &setup)
{
    if (setup.timeOfDay) {
        state.info.worldTime = *setup.timeOfDay;
    }
    state.info.daylightCycle = setup.daylightCycle;
    state.info.mobSpawning = setup.mobSpawning;
    state.info.randomTicks = setup.randomTicks;

    for (const QcWorldSetup::Mob &mob : setup.mobs) {
        if (const std::optional<int> type = state.content->mobIndex(mob.mob)) {
            state.mobs.spawn(*type, mob.at, state.newEntityId(), false, true);
        } else {
            qCWarning(lcSetup) << "World setup: unknown creature" << mob.mob;
        }
    }

    for (const QcWorldSetup::Stand &stand : setup.stands) {
        placeStand(state, stand);
    }

    for (const QcWorldSetup::Contents &contents : setup.contents) {
        fill(state, contents);
    }
    for (const QcBlockPos &pos : setup.growing) {
        state.plants.saplingPlaced(pos, blockAt(state, pos));
    }

    updates.setCommandMode(true);
    for (const QcBlockPos &pos : setup.updates) {
        updates.queue(pos);
    }
    updates.setCommandMode(false);
    state.info.setupDone = true;
}

} // namespace QcServerSetup
