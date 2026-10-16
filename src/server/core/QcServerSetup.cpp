#include "QcServerSetup.h"

#include "QcBlockUpdateQueue.h"
#include "QcMinecartMotion.h"
#include "QcPhysics.h"
#include "QcRails.h"
#include "QcServerState.h"
#include "QcStandProperties.h"

#include <QLoggingCategory>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

Q_LOGGING_CATEGORY(lcSetup, "qtcraft.server")

QcBlockId blockAt(QcServerState &state, QcBlockPos pos)
{
    const std::shared_ptr<const QcChunk> chunk = state.world().loadChunk(pos.chunk());
    return chunk->block(pos.localX(), pos.y, pos.localZ());
}

QList<QcItemStack> stacksOf(const QcServerState &state, const QList<QcStructureDefinition::Stack> &items)
{
    QList<QcItemStack> stacks;
    for (const QcStructureDefinition::Stack &item : items) {
        const std::optional<QcItemId> id = state.content().itemId(item.item);
        if (!id) {
            qCWarning(lcSetup) << "World setup: unknown item" << item.item;
            continue;
        }
        stacks.append({*id, std::min(item.count, state.content().item(*id).maxStack), 0});
    }
    return stacks;
}

// A chest gets the stacks in its slots, a machine as input, fuel and output.
void fill(QcServerState &state, const QcWorldSetup::Contents &contents)
{
    const QcBlockId block = blockAt(state, contents.at);
    const QList<QcItemStack> stacks = stacksOf(state, contents.items);
    QcBlockEntities &entities = state.blockEntities();
    if (!entities.ensure(contents.at, block)) {
        qCWarning(lcSetup) << "World setup: no chest or machine for contents at" << contents.at.x << contents.at.y << contents.at.z;
        return;
    }

    if (QcBlockEntities::Container *container = entities.container(contents.at)) {
        for (qsizetype i = 0; i < stacks.size() && i < container->stacks.size(); ++i) {
            container->stacks[i] = stacks[i];
        }
    } else if (QcBlockEntities::Machine *machine = entities.machine(contents.at)) {
        for (qsizetype i = 0; i < stacks.size() && i < QcBlockEntities::Machine::slotCount; ++i) {
            machine->stacks[std::size_t(i)] = stacks[i];
        }
    }
    state.noteContents(contents.at);
}

// An armour stand of a structure; properties that do not apply are reported and left out.
void placeStand(QcServerState &state, const QcWorldSetup::Stand &setup)
{
    QcArmourStand stand;
    stand.id = state.entities().newId();
    stand.motion.position = setup.at;
    stand.yaw = qcNormalizedAngle(setup.yaw);
    stand.item = QcStandProperties::standItem(state.content());
    for (const QString &property : setup.properties) {
        if (!QcStandProperties::applyLine(state.content(), stand, property)) {
            qCWarning(lcSetup) << "World setup: armour stand property" << property << "does not apply";
        }
    }
    if (!state.entities().stands().add(stand)) {
        qCWarning(lcSetup) << "World setup: too many armour stands";
    }
}

// A minecart of the setup standing in the middle of its rail.
void placeMinecart(QcServerState &state, const QcWorldSetup::Cart &setup)
{
    const QcBlockPos cell = setup.at;
    const std::optional<QcRails::Track> track = QcRails::trackOf(state.content(), blockAt(state, cell));
    const std::optional<QcItemId> item = state.content().itemId(setup.item);
    if (!track || !item || !state.content().item(*item).placesCart) {
        qCWarning(lcSetup) << "World setup: no rail or no cart item for a minecart at" << cell.x << cell.y << cell.z << setup.item;
        return;
    }

    QcMinecart cart = state.entities().minecarts().fromItem(state.content().item(*item).name);
    cart.id = state.entities().newId();
    const QcVec3 from = QcRails::endPoint(cell, track->ends[0]);
    const QcVec3 to = QcRails::endPoint(cell, track->ends[1]);
    cart.motion.position = (from + to) * 0.5;
    cart.motion.onGround = true;
    cart.yaw = QcMinecartMotion::yawAlong(to - from);
    if (!state.entities().minecarts().add(cart)) {
        qCWarning(lcSetup) << "World setup: too many minecarts";
    }
}

// Contents at a rail with a container cart go into the cart (decision E-386); true when they did.
bool fillCart(QcServerState &state, const QcWorldSetup::Contents &contents)
{
    for (QcMinecart &cart : state.entities().minecarts().editableAll()) {
        if (cart.stacks.isEmpty() || QcPhysics::blockContaining(cart.motion.position) != contents.at) {
            continue;
        }
        const QList<QcItemStack> stacks = stacksOf(state, contents.items);
        for (qsizetype i = 0; i < stacks.size() && i < cart.stacks.size(); ++i) {
            cart.stacks[i] = stacks[i];
        }
        return true;
    }
    return false;
}

} // namespace

namespace QcServerSetup {

void apply(QcServerState &state, QcBlockUpdateQueue &updates, const QcWorldSetup &setup)
{
    if (setup.timeOfDay) {
        state.info().worldTime = *setup.timeOfDay;
    }
    state.info().daylightCycle = setup.daylightCycle;
    state.info().mobSpawning = setup.mobSpawning;
    state.info().randomTicks = setup.randomTicks;
    state.info().fireSpread = setup.fireSpread;
    state.info().weatherCycle = setup.weatherCycle;

    for (const QcWorldSetup::Mob &mob : setup.mobs) {
        if (const std::optional<int> type = state.content().mobIndex(mob.mob)) {
            state.entities().creatures().spawn(*type, mob.at, state.entities().newId(), false, true);
        } else {
            qCWarning(lcSetup) << "World setup: unknown creature" << mob.mob;
        }
    }

    for (const QcWorldSetup::Stand &stand : setup.stands) {
        placeStand(state, stand);
    }

    for (const QcWorldSetup::Cart &cart : setup.minecarts) {
        placeMinecart(state, cart);
    }
    for (const QcWorldSetup::Contents &contents : setup.contents) {
        if (!fillCart(state, contents)) {
            fill(state, contents);
        }
    }
    for (const QcBlockPos &pos : setup.growing) {
        state.plants().saplingPlaced(state.blockEntities(), pos, blockAt(state, pos));
    }

    updates.setCommandMode(true);
    for (const QcBlockPos &pos : setup.updates) {
        updates.queue(pos);
        // Circuits of demos start too (decision E-340).
        state.circuits().wake(state.world(), pos);
    }
    updates.setCommandMode(false);
    state.info().setupDone = true;
}

} // namespace QcServerSetup
