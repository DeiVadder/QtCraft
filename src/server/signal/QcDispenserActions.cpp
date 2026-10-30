// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/signal/QcDispenserActions.h"

#include "common/blockrules/QcBlockRules.h"
#include "common/content/registry/QcSoundKeys.h"
#include "common/gameplay/QcArmour.h"
#include "common/physics/QcPhysics.h"
#include "server/animals/QcServerAnimals.h"
#include "server/blocks/QcLiquidFlow.h"
#include "server/blocks/QcServerBlocks.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/signal/QcSignalTable.h"
#include "worldgen/terrain/QcNoise.h"

#include <QRandomGenerator>

#include <algorithm>
#include <limits>
#include <numeric>

namespace {

QcVec3 centreOf(QcBlockPos pos)
{
    return {pos.x + 0.5, pos.y + 0.5, pos.z + 0.5};
}

QcAabb cellBox(QcBlockPos pos)
{
    const QcVec3 corner{static_cast<double>(pos.x), static_cast<double>(pos.y), static_cast<double>(pos.z)};
    return {corner, corner + QcVec3{1.0, 1.0, 1.0}};
}

} // namespace

QcDispenserActions::QcDispenserActions(QcServerState &state, QcServerBlocks &blocks, QcServerSounds &sounds, const QcSignalTable &table)
    : m_state(state)
    , m_blocks(blocks)
    , m_sounds(sounds)
    , m_table(table)
{
}

std::optional<QcDispenserActions::Shot> QcDispenserActions::aim(QcBlockPos pos) const
{
    const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
    const QcSignalState &state = m_table.at(block.value_or(qcAirBlockId));
    if (state.kind != QcSignalKind::Dispenser || !state.facing) {
        return std::nullopt;
    }

    const QcInventoryAccess access(m_state);
    const QList<QcItemStack> stacks = access.slotsAt(pos);
    QList<int> filled;
    for (qsizetype slot = 0; slot < stacks.size(); ++slot) {
        if (!stacks[slot].isEmpty()) {
            filled.append(static_cast<int>(slot));
        }
    }
    if (filled.isEmpty()) {
        return std::nullopt;
    }

    // Seeded with the world, the tick and the contents: the same world picks the same at any place
    // and in any rotation, and dispensers with other contents do not all pick alike. Items enter
    // the hash by name, not by their load-order id, so new content (also from mods) does not change
    // what a dispenser fires.
    const QcContentRegistry &content = m_state.content();
    quint64 contents = 0;
    for (const QcItemStack &stack : stacks) {
        const quint64 item = stack.isEmpty() || !content.isValidItem(stack.item) ? 0 : qcNameSalt(content.item(stack.item).name);
        contents = contents * 0x100000001B3ULL ^ (item << 20 ^ static_cast<quint64>(stack.count));
    }
    const quint64 seed = static_cast<quint64>(m_state.config().seed) * 0x9E3779B97F4A7C15ULL ^ m_state.clock().tick() * 0xBF58476D1CE4E5B9ULL ^ contents;
    QRandomGenerator random(static_cast<quint32>(seed ^ (seed >> 32)));
    const int slot = filled[static_cast<qsizetype>(random.bounded(static_cast<quint32>(filled.size())))];
    return Shot{pos, *state.facing, slot, stacks[slot].withCount(1)};
}

qsizetype QcDispenserActions::fire(const QList<QcBlockPos> &positions)
{
    QcInventoryAccess access(m_state);
    QList<Shot> shots;
    for (const QcBlockPos &pos : positions) {
        if (std::optional<Shot> shot = aim(pos)) {
            shots.append(*shot);
        } else if (m_table.at(m_state.world().blockAt(pos).value_or(qcAirBlockId)).kind == QcSignalKind::Dispenser) {
            m_sounds.play(QString::fromLatin1(QcSoundKeys::dispenserFail), centreOf(pos));
        }
    }

    const QList<Shot> others = insertDropped(shots, access);
    const QList<Shot> thrown = use(others, access);
    for (const Shot &shot : thrown) {
        throwItem(shot, access);
    }
    access.finish();
    return positions.size();
}

QList<QcDispenserActions::Shot> QcDispenserActions::insertDropped(const QList<Shot> &shots, QcInventoryAccess &access)
{
    // Droppers put the item into a container in front, all of a step together; dispensers use or
    // throw it whatever stands in front (as in the reference).
    QList<QcInventoryAccess::Insertion> insertions;
    QList<Shot> others;
    for (qsizetype i = 0; i < shots.size(); ++i) {
        const QcBlockPos front = qcNeighbour(shots[i].dispenser, shots[i].facing);
        const bool dropper = !m_state.content().block(m_state.world().blockAt(shots[i].dispenser).value_or(qcAirBlockId)).signal.usesItems;
        if (dropper && access.hasInventory(front) && access.ensure(front)) {
            insertions.append({front, QcBlockRules::opposite(shots[i].facing), shots[i].unit, static_cast<int>(i), -1});
        } else {
            others.append(shots[i]);
        }
    }

    QSet<int> done;
    const QList<QcInventoryAccess::Insertion> kept = access.resolve(insertions);
    for (const QcInventoryAccess::Insertion &insertion : kept) {
        const Shot &shot = shots[insertion.giver];
        if (access.put(insertion.target, insertion.slot, insertion.stack) && access.take(shot.dispenser, shot.slot, 1)) {
            done.insert(insertion.giver);
            m_sounds.play(QString::fromLatin1(QcSoundKeys::dispenserDispense), centreOf(shot.dispenser));
        }
    }
    // A full container (or a conflict): nothing happens, as for a dropper in the reference.
    for (const QcInventoryAccess::Insertion &insertion : std::as_const(insertions)) {
        if (!done.contains(insertion.giver)) {
            m_sounds.play(QString::fromLatin1(QcSoundKeys::dispenserFail), centreOf(shots[insertion.giver].dispenser));
        }
    }
    return others;
}

QList<QcDispenserActions::Shot> QcDispenserActions::use(const QList<Shot> &shots, QcInventoryAccess &access)
{
    const QcContentRegistry &content = m_state.content();
    QList<bool> acts;
    QHash<QcBlockPos, int> actionsOn;
    for (const Shot &shot : shots) {
        const bool uses = content.block(m_state.world().blockAt(shot.dispenser).value_or(qcAirBlockId)).signal.usesItems;
        const QcDispenseAction action = content.item(shot.unit.item).dispense;
        acts.append(uses && action != QcDispenseAction::Throw);
        // A shot needs no cell of its own: arrows of two dispensers may fly through the same one.
        if (acts.constLast() && action != QcDispenseAction::Shoot) {
            ++actionsOn[qcNeighbour(shot.dispenser, shot.facing)];
        }
    }

    QList<Shot> thrown;
    for (qsizetype i = 0; i < shots.size(); ++i) {
        const Shot &shot = shots[i];
        // Two actions on the same cell in one step: neither happens (no order decides).
        if (acts[i] && content.item(shot.unit.item).dispense != QcDispenseAction::Shoot
            && actionsOn.value(qcNeighbour(shot.dispenser, shot.facing)) > 1) {
            m_sounds.play(QString::fromLatin1(QcSoundKeys::dispenserFail), centreOf(shot.dispenser));
            continue;
        }
        if (!acts[i]) {
            thrown.append(shot);
        } else if (!useOne(shot, access)) {
            // Flint and steel that lights nothing stays in the dispenser, as in the reference.
            if (content.item(shot.unit.item).dispense == QcDispenseAction::Ignite) {
                m_sounds.play(QString::fromLatin1(QcSoundKeys::dispenserFail), centreOf(shot.dispenser));
            } else {
                thrown.append(shot);
            }
        }
    }
    return thrown;
}

bool QcDispenserActions::useOne(const Shot &shot, QcInventoryAccess &access)
{
    switch (m_state.content().item(shot.unit.item).dispense) {
    case QcDispenseAction::PlaceLiquid:
        return placeLiquid(shot, access);
    case QcDispenseAction::Scoop:
        return scoopLiquid(shot, access);
    case QcDispenseAction::Equip:
        return equip(shot, access);
    case QcDispenseAction::Ignite:
        return ignite(shot, access);
    case QcDispenseAction::Shoot:
        return shoot(shot, access);
    case QcDispenseAction::Throw:
        break;
    }
    return false;
}

bool QcDispenserActions::placeLiquid(const Shot &shot, QcInventoryAccess &access)
{
    const QcContentRegistry &content = m_state.content();
    const QcItemDefinition &item = content.item(shot.unit.item);
    const QcBlockPos front = qcNeighbour(shot.dispenser, shot.facing);
    const std::optional<QcBlockId> liquid = content.blockId(item.placesBlock);
    const std::optional<QcBlockId> there = front.isInsideWorldHeight() ? m_state.world().blockAt(front) : std::nullopt;
    // Only liquids, only into a cell inside the world that gives way (air, flowing liquid, tall
    // grass), and only when the bucket has an empty one to become.
    const bool source = there && content.block(*there).liquid && QcLiquidFlow::isSource(content, *there);
    const bool replaceable = there && content.block(*there).replaceable && !source;
    if (!liquid || !content.block(*liquid).liquid || !replaceable || !content.itemId(item.emptiesTo)) {
        return false;
    }
    // A fish bucket whose fish may not come out stays full (decision E-501).
    if (!QcServerAnimals::mayRelease(m_state, item, QcServerAnimals::releasePoint(front))) {
        return false;
    }

    if (!m_blocks.setQuietly(front, *liquid)) {
        return false;
    }
    m_blocks.sendBlock(front, *liquid);
    // It lets its fish out where it pours, kept with the world as from a player's hand.
    QcServerAnimals::release(m_state, item, QcServerAnimals::releasePoint(front));
    m_sounds.play(QString::fromLatin1(QcSoundKeys::bucketEmpty), centreOf(front));
    exchange(shot, item.emptiesTo, access);
    return true;
}

bool QcDispenserActions::scoopLiquid(const Shot &shot, QcInventoryAccess &access)
{
    const QcContentRegistry &content = m_state.content();
    const QcBlockPos front = qcNeighbour(shot.dispenser, shot.facing);
    const std::optional<QcBlockId> there = m_state.world().blockAt(front);
    if (!there || !content.block(*there).liquid || !QcLiquidFlow::isSource(content, *there)) {
        return false;
    }

    const QString bucket = filledBucket(shot.unit.item, *there);
    if (bucket.isEmpty() || !m_blocks.setQuietly(front, qcAirBlockId)) {
        return false;
    }
    m_blocks.sendBlock(front, qcAirBlockId);
    m_sounds.play(QString::fromLatin1(QcSoundKeys::bucketFill), centreOf(front));
    exchange(shot, bucket, access);
    return true;
}

QString QcDispenserActions::filledBucket(QcItemId empty, QcBlockId liquid) const
{
    // The filled item that empties into this one and pours this liquid (never one with a creature
    // inside, a fish bucket, decision E-501); else the liquid's bucket.
    const QcContentRegistry &content = m_state.content();
    const QString &emptyName = content.item(empty).name;
    for (const QcItemDefinition &item : content.items()) {
        const std::optional<QcBlockId> places = content.blockId(item.placesBlock);
        if (item.emptiesTo == emptyName && item.releasesMob.isEmpty() && places && content.isSameBlock(*places, liquid)) {
            return item.name;
        }
    }
    const QString &bucket = content.block(liquid).flow.bucket;
    return content.itemId(bucket) ? bucket : QString();
}

bool QcDispenserActions::equip(const Shot &shot, QcInventoryAccess &access)
{
    const QcArmourDefinition *armour = QcArmour::of(m_state.content(), shot.unit.item);
    if (!armour) {
        return false;
    }

    // The body in front with that slot free and the lowest entity id: players and stands alike.
    const QcAabb cell = cellBox(qcNeighbour(shot.dispenser, shot.facing));
    QcServerClient *player = nullptr;
    QcArmourStand *stand = nullptr;
    quint32 lowest = std::numeric_limits<quint32>::max();
    for (const auto &client : m_state.clients()) {
        const QcAabb body = QcPhysics::boxAt(client->motion.position, QcPhysics::playerWidth, QcPhysics::playerHeight);
        if (client->hasBody() && body.intersects(cell)
            && client->equipment.at(armour->slot).isEmpty() && client->id < lowest) {
            lowest = client->id;
            player = client.get();
        }
    }
    for (const QcArmourStand &candidate : m_state.entities().stands().stands()) {
        if (QcArmourStandManager::bodyOf(candidate).intersects(cell) && candidate.equipment.at(armour->slot).isEmpty() && candidate.id < lowest) {
            lowest = candidate.id;
            player = nullptr;
            stand = m_state.entities().stands().find(candidate.id);
        }
    }
    if (!player && !stand) {
        return false;
    }

    // The shot's slot is the one aim() found filled, so taking from it cannot fail.
    [[maybe_unused]] const bool taken = access.take(shot.dispenser, shot.slot, 1);
    if (stand) {
        stand->equipment.at(armour->slot) = shot.unit;
        m_state.entities().markStale(stand->id);
    } else if (m_equipPlayer) {
        QcEquipment equipment = player->equipment;
        equipment.at(armour->slot) = shot.unit;
        m_equipPlayer(*player, equipment);
    }
    m_sounds.play(QString::fromLatin1(QcSoundKeys::armourEquip), centreOf(shot.dispenser));
    return true;
}

bool QcDispenserActions::ignite(const Shot &shot, QcInventoryAccess &access)
{
    const QcItemDefinition &item = m_state.content().item(shot.unit.item);
    if (!m_ignite || !m_ignite(qcNeighbour(shot.dispenser, shot.facing))) {
        return false;
    }

    // The shot's slot is the one aim() found filled, so taking from it cannot fail; the worn tool
    // goes back into it (a stack of one, so the slot is empty now).
    [[maybe_unused]] const bool taken = access.take(shot.dispenser, shot.slot, 1);
    QcItemStack worn = shot.unit;
    if (item.durability > 0 && ++worn.damage >= item.durability) {
        return true;
    }
    if (!access.put(shot.dispenser, shot.slot, worn)) {
        Shot rest = shot;
        rest.unit = worn;
        rest.slot = -1;
        throwItem(rest, access);
    }
    return true;
}

bool QcDispenserActions::shoot(const Shot &shot, QcInventoryAccess &access)
{
    // Straight out of the front, just outside it, without spread: a trap hits where it points.
    const QcFaceOffset offset = qcFaceOffset(shot.facing);
    const QcVec3 along{static_cast<double>(offset.dx), static_cast<double>(offset.dy), static_cast<double>(offset.dz)};
    QcProjectileManager::Launch launch;
    launch.type = m_state.content().item(shot.unit.item).projectile;
    launch.position = centreOf(shot.dispenser) + along * shotOffset;
    launch.velocity = along * shotSpeed;
    if (!m_state.entities().projectiles().launch(launch, m_state.entities().newId())) {
        return false;
    }

    // The shot's slot is the one aim() found filled, so taking from it cannot fail.
    [[maybe_unused]] const bool taken = access.take(shot.dispenser, shot.slot, 1);
    m_sounds.play(QString::fromLatin1(QcSoundKeys::dispenserDispense), centreOf(shot.dispenser));
    return true;
}

void QcDispenserActions::exchange(const Shot &shot, const QString &name, QcInventoryAccess &access)
{
    // The shot's slot is the one aim() found filled, so taking from it cannot fail.
    [[maybe_unused]] const bool taken = access.take(shot.dispenser, shot.slot, 1);
    const std::optional<QcItemId> item = m_state.content().itemId(name);
    if (!item) {
        return;
    }

    const QcItemStack result{*item, 1, 0};
    QList<QcItemStack> stacks = access.slotsAt(shot.dispenser);
    if (stacks[shot.slot].count <= 0) {
        // The emptied slot takes it (it exists: the unit came from it).
        [[maybe_unused]] const bool put = access.put(shot.dispenser, shot.slot, result);
        return;
    }
    QList<int> cells(stacks.size());
    std::iota(cells.begin(), cells.end(), 0);
    const int slot = access.slotFor(shot.dispenser, stacks, cells, result);
    if (slot >= 0) {
        // slotFor() chose a slot of this dispenser, so it exists.
        [[maybe_unused]] const bool put = access.put(shot.dispenser, slot, result);
    } else {
        Shot rest = shot;
        rest.unit = result;
        rest.slot = -1;
        throwItem(rest, access);
    }
}

void QcDispenserActions::throwItem(const Shot &shot, QcInventoryAccess &access)
{
    if (shot.slot >= 0) {
        // The shot's slot is the one aim() found filled, so taking from it cannot fail.
        [[maybe_unused]] const bool taken = access.take(shot.dispenser, shot.slot, 1);
    }

    const QcFaceOffset offset = qcFaceOffset(shot.facing);
    const QcVec3 along{static_cast<double>(offset.dx), static_cast<double>(offset.dy), static_cast<double>(offset.dz)};
    QcItemEntityManager::Drop drop;
    drop.stack = shot.unit;
    // At the front face, just outside it (also in front of a block, as in the reference).
    drop.position = centreOf(shot.dispenser) + along * 0.6 - QcVec3{0.0, QcItemEntityManager::itemSize * 0.5, 0.0};
    drop.velocity = along * throwSpeed + QcVec3{0.0, offset.dy == 0 ? throwLift : 0.0, 0.0};
    m_state.entities().items().spawn(drop, m_state.entities().newId());
    m_sounds.play(QString::fromLatin1(QcSoundKeys::dispenserDispense), centreOf(shot.dispenser));
}
