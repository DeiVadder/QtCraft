// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/spawners/QcFurnishing.h"

#include "common/blockrules/QcRails.h"
#include "common/content/registry/QcContentLoot.h"
#include "server/core/QcServerLogging.h"
#include "server/core/QcServerState.h"
#include "server/minecarts/QcMinecartMotion.h"


namespace {

// The loot of `table` drawn with `seed`, spread over `slotCount` slots.
QList<QcItemStack> lootFor(const QcContentRegistry &content, const QcLootTableDefinition &table, quint64 seed, int slotCount)
{
    return QcLootTables::scatter(QcLootTables::draw(table, seed, QcContentLoot::itemsOf(content)), slotCount, seed);
}

// Fills the chest at `chest.at` with its loot table's draw; a chest that holds something already
// (a save from before the mark) is left as it is.
void fillChest(QcServerState &state, const QcChunkFurnishing::Chest &chest)
{
    QcBlockEntities::Container *container = state.blockEntities().container(chest.at);
    const QcLootTableDefinition *table = state.content().lootTables().find(chest.lootTable);
    if (!table) {
        qCWarning(lcServer) << "Unknown loot table" << chest.lootTable << "for the chest at" << chest.at.x << chest.at.y << chest.at.z;
        return;
    }
    if (!container || std::ranges::any_of(container->stacks, [](const QcItemStack &stack) { return !stack.isEmpty(); })) {
        return;
    }
    container->stacks = lootFor(state.content(), *table, chest.seed, static_cast<int>(container->stacks.size()));
}

// Puts a minecart in the middle of the rail at `cart.at` (decision E-570), a container cart filled
// from its loot table; nothing where the rail is gone (a world made before the generator had it).
void placeCart(QcServerState &state, const QcChunkFurnishing::Cart &setup)
{
    const QcContentRegistry &content = state.content();
    const std::shared_ptr<const QcChunk> chunk = state.world().chunk(setup.at.chunk());
    const QcBlockId block = chunk ? chunk->block(setup.at.localX(), setup.at.y, setup.at.localZ()) : qcAirBlockId;
    const std::optional<QcRails::Track> track = QcRails::trackOf(content, block);
    const std::optional<QcItemId> item = content.itemId(setup.item);
    if (!track || !item || !content.item(*item).placesCart) {
        return;
    }

    QcMinecart cart = state.entities().minecarts().fromItem(content.item(*item).name);
    cart.id = state.entities().newId();
    const QcVec3 from = QcRails::endPoint(setup.at, track->ends[0]);
    const QcVec3 to = QcRails::endPoint(setup.at, track->ends[1]);
    cart.motion.position = (from + to) * 0.5;
    cart.motion.onGround = true;
    cart.yaw = QcMinecartMotion::yawAlong(to - from);
    if (const QcLootTableDefinition *table = content.lootTables().find(setup.lootTable); table && !cart.stacks.isEmpty()) {
        cart.stacks = lootFor(content, *table, setup.seed, static_cast<int>(cart.stacks.size()));
    }
    if (!state.entities().minecarts().add(cart)) {
        qCWarning(lcServer) << "Too many minecarts for the cart at" << setup.at.x << setup.at.y << setup.at.z;
    }
}

// Stands the creature of `resident` in its cell, kept with the world: it comes once, with the
// chunk (decision E-1089).
void placeResident(QcServerState &state, const QcChunkFurnishing::Resident &resident)
{
    const std::optional<QcCreatureKindId> type = state.content().mobKind(resident.mob);
    if (!type) {
        qCWarning(lcServer) << "Unknown creature" << resident.mob << "for the structure at" << resident.at.x << resident.at.y << resident.at.z;
        return;
    }
    const QcVec3 feet{resident.at.x + 0.5, static_cast<double>(resident.at.y), resident.at.z + 0.5};
    state.entities().creatures().spawn(*type, feet, state.entities().newId(), false, true);
}

// What the generator has for the chunk at `pos` when it should be furnished now; none when it is
// furnished already, not loaded or has nothing to put in.
std::optional<QcChunkFurnishing> furnishingDue(QcServerState &state, QcChunkPos pos)
{
    QcServerWorld &world = state.world();
    if (world.blockEntityExtras().isFurnished(pos) || !world.chunk(pos)) {
        return std::nullopt;
    }
    // Without a spawner, a block with storage, a rail or a resident there is nothing to put in; most
    // chunks end here, before the generator is asked (this runs on the tick thread).
    const QcSectionMarks &marks = world.marks();
    if (marks.sectionsOf(pos, QcBlockTrait::Spawner) == 0 && marks.sectionsOf(pos, QcBlockTrait::BlockEntity) == 0
        && marks.sectionsOf(pos, QcBlockTrait::Rail) == 0 && !world.generator().hasResidentsIn(pos)) {
        return std::nullopt;
    }
    QcChunkFurnishing furnishing = world.generator().furnishingIn(pos);
    if (furnishing.isEmpty()) {
        // Furnished with nothing: a chunk whose blocks come back unchanged (generated again) does not
        // ask again.
        world.blockEntityExtras().noteFurnished(pos);
        return std::nullopt;
    }
    return furnishing;
}

// Fills the chests and spawners and puts the carts of `furnishing` in the chunk at `pos`.
void placeThings(QcServerState &state, const QcChunkFurnishing &furnishing, QcChunkPos pos)
{
    for (const QcChunkFurnishing::Chest &chest : furnishing.chests) {
        if (chest.at.chunk() == pos) {
            fillChest(state, chest);
        }
    }
    for (const QcStructureDefinition::Spawner &spawner : furnishing.spawners) {
        if (spawner.at.chunk() == pos) {
            state.world().spawners().set({spawner.at, spawner.mob, QcSpawners::firstDelay, spawner.confined});
        }
    }
    for (const QcChunkFurnishing::Cart &cart : furnishing.carts) {
        if (cart.at.chunk() == pos) {
            placeCart(state, cart);
        }
    }
}

} // namespace

namespace QcFurnishing {

void furnish(QcServerState &state, QcChunkPos pos)
{
    const std::optional<QcChunkFurnishing> furnishing = furnishingDue(state, pos);
    if (!furnishing) {
        return;
    }
    placeThings(state, *furnishing, pos);
    for (const QcChunkFurnishing::Resident &resident : furnishing->residents) {
        if (resident.at.chunk() == pos) {
            placeResident(state, resident);
        }
    }
    state.world().blockEntityExtras().noteFurnished(pos);
}

void furnishAgain(QcServerState &state, QcChunkPos pos)
{
    const std::optional<QcChunkFurnishing> furnishing = furnishingDue(state, pos);
    if (!furnishing) {
        return;
    }
    placeThings(state, *furnishing, pos);
    state.world().blockEntityExtras().noteFurnished(pos);
}

} // namespace QcFurnishing
