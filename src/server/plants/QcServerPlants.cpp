#include "server/plants/QcServerPlants.h"

#include "common/blockrules/QcBlockRules.h"
#include "common/physics/QcPhysics.h"
#include "server/blocks/QcServerBlocks.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcLineOfSight.h"
#include "server/gameplay/QcServerInventory.h"

#include <QHash>

#include <array>

using namespace Qt::StringLiterals;

QcServerPlants::QcServerPlants(QcServerState &state, QcServerSounds &sounds, QcServerBlocks &blocks, QcServerInventory &inventory)
    : m_state(state)
    , m_sounds(sounds)
    , m_blocks(blocks)
    , m_inventory(inventory)
    , m_vegetation(state.content())
{
    // Items that plant themselves: they place a crop with a plantChance (decision E-294).
    const QcContentRegistry &content = m_state.content();
    for (QcItemId id = 1; content.isValidItem(id); ++id) {
        const std::optional<QcBlockId> crop = content.blockId(content.item(id).placesBlock);
        if (crop && content.block(*crop).crop.plantChance > 0.0) {
            m_seedCrops.insert(id, *crop);
        }
    }
}

void QcServerPlants::plantItems(const QList<QcItemEntity> &despawned)
{
    for (const QcItemEntity &item : despawned) {
        tryPlant(item);
    }
    if (++m_itemTicks % seedCheckTicks == 0) {
        plantSeeds();
    }
}

void QcServerPlants::plantSeeds()
{
    struct Planting
    {
        quint32 item = 0;
        QcBlockPos pos;
        QcBlockId crop = qcAirBlockId;
    };

    QList<Planting> plantings;
    // Cells planned in this check: two stacks never plant into one cell.
    QSet<QcBlockPos> planned;
    for (const QcItemEntity &item : m_state.entities().items().items()) {
        const auto crop = m_seedCrops.constFind(item.stack.item);
        if (item.surplus || !item.resting || crop == m_seedCrops.constEnd()) {
            continue;
        }

        if (const std::optional<QcBlockPos> target = seedTarget(item, *crop, planned)) {
            plantings.append({item.id, *target, *crop});
            planned.insert(*target);
        }
    }

    // Planted afterwards: taking items changes the item list.
    for (const Planting &planting : std::as_const(plantings)) {
        if (m_state.world().blockAt(planting.pos) != qcAirBlockId || !m_state.entities().items().takeOne(planting.item)) {
            continue;
        }
        m_blocks.setAndBroadcast(planting.pos, planting.crop);
        m_sounds.playBlock(m_state.content().block(planting.crop), QcBlockSoundAction::Place, planting.pos);
    }
}

std::optional<QcBlockPos> QcServerPlants::seedTarget(const QcItemEntity &item, QcBlockId crop,
                                                             const QSet<QcBlockPos> &planned)
{
    // The item lies on the top of the block below its feet (farmland is lower than a full block).
    // Its own cell comes first; a stack whose cell is taken (or that keeps more seeds after
    // planting it) reaches the free soil around it within the merge radius, so merged drops still
    // replant a whole field. Each cell rolls its own chance; one seed per stack and check.
    static constexpr std::array<std::pair<int, int>, 9> cells{
        {{0, 0}, {0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}}};
    const QcBlockPos ground = QcPhysics::blockContaining(item.motion.position - QcVec3{0.0, 0.1, 0.0});
    const double chance = m_state.content().block(crop).crop.plantChance;
    for (const auto &[dx, dz] : cells) {
        const QcBlockPos soil = ground.offset(dx, 0, dz);
        if (!planned.contains(soil.offset(0, 1, 0)) && isFreeSoil(soil, crop) && m_state.random().generateDouble() < chance) {
            return soil.offset(0, 1, 0);
        }
    }
    return std::nullopt;
}

bool QcServerPlants::isFreeSoil(QcBlockPos ground, QcBlockId crop) const
{
    const QcBlockPos target = ground.offset(0, 1, 0);
    const std::optional<QcBlockId> soil = m_state.world().blockAt(ground);
    if (!soil || !m_state.content().isValidBlock(*soil) || !m_state.content().block(*soil).soil.isSoil()
        || m_state.world().blockAt(target) != qcAirBlockId || !target.isInsideWorldHeight()) {
        return false;
    }
    return QcBlockRules::isSupported(m_state.world(), target, crop);
}

void QcServerPlants::tick()
{
    const double dt = QcPhysics::tickDuration;
    const auto setBlock = [this](QcBlockPos pos, QcBlockId block) {
        m_blocks.setAndBroadcast(pos, block);
        return m_state.world().blockAt(pos) == block;
    };
    m_state.plants().tick(m_state.world(), m_state.blockEntities(), dt,
                        {[this](QcBlockPos pos) { return m_state.isLoadedAt(pos); }, setBlock,
                         [this](QcBlockPos pos) { m_blocks.breakBlock(pos, true); }});
    if (m_state.info().randomTicks) {
        m_state.growth().tick(m_state.world(), setBlock);
    }
}

void QcServerPlants::tryPlant(const QcItemEntity &item)
{
    const QcContentRegistry &content = m_state.content();
    const QcItemDefinition &definition = content.item(item.stack.item);
    const std::optional<QcBlockId> sapling = content.blockId(definition.placesBlock);
    if (!sapling || !content.block(*sapling).sapling.isSapling()
        || m_state.random().generateDouble() >= content.block(*sapling).sapling.plantChance) {
        return;
    }

    const QcBlockPos pos = QcPhysics::blockContaining(item.motion.position + QcVec3{0.0, 0.05, 0.0});
    const std::optional<QcBlockId> existing = m_state.world().blockAt(pos);
    if (!existing || !pos.isInsideWorldHeight() || !content.block(*existing).replaceable || content.block(*existing).liquid
        || !m_state.plants().canPlant(*sapling, m_state.world().blockAt(pos.offset(0, -1, 0)))) {
        return;
    }

    // Setting the sapling starts its timer (QcServerBlocks::setQuietly).
    m_blocks.setAndBroadcast(pos, *sapling);
}

bool QcServerPlants::till(QcServerClient &client, QcBlockPos target, QcBlockFace face, const QcItemDefinition *held)
{
    const QcContentRegistry &content = m_state.content();
    const std::optional<QcBlockId> clicked = m_state.world().blockAt(target);
    if (!held || !held->tills || !clicked || !content.isValidBlock(*clicked) || content.block(*clicked).tillsInto.isEmpty()) {
        return false;
    }

    const std::optional<QcBlockId> soil = content.blockId(content.block(*clicked).tillsInto);
    const bool open = m_state.world().blockAt(target.offset(0, 1, 0)) == qcAirBlockId;
    if (!soil || face == QcBlockFace::Down || !open || !QcLineOfSight::isWithinReach(client, target)) {
        return false;
    }

    // Wet at once next to water, so a fresh field does not wait for its first random tick.
    m_blocks.setAndBroadcast(target, m_state.growth().crops().freshSoil(m_state.world(), target, *soil));
    m_sounds.playBlock(content.block(*soil), QcBlockSoundAction::Place, target);
    m_inventory.wearHeld(client);
    return true;
}

bool QcServerPlants::flatten(QcServerClient &client, QcBlockPos target, QcBlockFace face, const QcItemDefinition *held)
{
    const QcContentRegistry &content = m_state.content();
    const std::optional<QcBlockId> clicked = m_state.world().blockAt(target);
    if (!held || !held->flattens || !clicked || !content.isValidBlock(*clicked) || content.block(*clicked).flattensInto.isEmpty()) {
        return false;
    }

    const std::optional<QcBlockId> path = content.blockId(content.block(*clicked).flattensInto);
    const bool open = m_state.world().blockAt(target.offset(0, 1, 0)) == qcAirBlockId;
    if (!path || face != QcBlockFace::Up || !open || !QcLineOfSight::isWithinReach(client, target)) {
        return false;
    }

    m_blocks.setAndBroadcast(target, *path);
    m_sounds.playBlock(content.block(*path), QcBlockSoundAction::Place, target);
    m_inventory.wearHeld(client);
    return true;
}

bool QcServerPlants::carve(QcServerClient &client, QcBlockPos target, QcBlockFace face, const QcItemDefinition *held)
{
    const QcContentRegistry &content = m_state.content();
    const std::optional<QcBlockId> clicked = m_state.world().blockAt(target);
    if (!held || !held->carves || !clicked || !content.isValidBlock(*clicked) || content.block(*clicked).carvesInto.isEmpty()
        || !QcLineOfSight::isWithinReach(client, target)) {
        return false;
    }

    const QcBlockDefinition &block = content.block(*clicked);
    const std::optional<QcBlockId> carved = content.blockId(block.carvesInto);
    if (!carved) {
        return false;
    }

    // The face looks out of the clicked side; clicked on the top or bottom, towards the player.
    const bool vertical = face == QcBlockFace::Up || face == QcBlockFace::Down;
    const QHash<QString, QString> towardsPlayer{{u"north"_s, u"south"_s}, {u"south"_s, u"north"_s}, {u"east"_s, u"west"_s}, {u"west"_s, u"east"_s}};
    const QString facing = vertical ? towardsPlayer.value(QcBlockRules::lookDirection(client.yaw), u"north"_s) : QcBlockRules::faceName(face);
    m_blocks.setAndBroadcast(target, content.withProperty(*carved, u"facing"_s, facing));
    m_sounds.playBlock(content.block(*carved), QcBlockSoundAction::Place, target);
    const std::optional<QcItemId> drop = block.carveDrop.isEmpty() ? std::nullopt : content.itemId(block.carveDrop);
    if (drop && block.carveDropCount > 0) {
        const QcFaceOffset offset = qcFaceOffset(face);
        const QcVec3 out{target.x + 0.5 + offset.dx * 0.7, target.y + 0.5 + offset.dy * 0.7, target.z + 0.5 + offset.dz * 0.7};
        m_state.entities().items().spawnScattered({*drop, block.carveDropCount, 0}, out, m_state.entities().newId());
    }
    m_inventory.wearHeld(client);
    return true;
}

bool QcServerPlants::strip(QcServerClient &client, QcBlockPos target, const QcItemDefinition *held)
{
    const QcContentRegistry &content = m_state.content();
    const std::optional<QcBlockId> clicked = m_state.world().blockAt(target);
    if (!held || !held->strips || !clicked || !content.isValidBlock(*clicked) || content.block(*clicked).strippedInto.isEmpty()
        || !QcLineOfSight::isWithinReach(client, target)) {
        return false;
    }

    const std::optional<QcBlockId> stripped = content.blockId(content.block(*clicked).strippedInto);
    if (!stripped) {
        return false;
    }

    QcBlockId state = *stripped;
    for (const QcBlockProperty &property : content.block(*stripped).properties) {
        const QString value = content.property(*clicked, property.name);
        if (property.values.contains(value)) {
            state = content.withProperty(state, property.name, value);
        }
    }
    m_blocks.setAndBroadcast(target, state);
    m_sounds.playBlock(content.block(state), QcBlockSoundAction::Place, target);
    m_inventory.wearHeld(client);
    return true;
}

bool QcServerPlants::fertilize(QcServerClient &client, QcBlockPos target, const QcItemDefinition *held)
{
    const QcContentRegistry &content = m_state.content();
    const std::optional<QcBlockId> clicked = m_state.world().blockAt(target);
    if (!held || !held->fertilizes || !clicked || !content.isValidBlock(*clicked) || client.dead
        || !QcLineOfSight::isWithinReach(client, target)) {
        return false;
    }

    const QcBlockDefinition &block = content.block(*clicked);
    QRandomGenerator &random = m_state.random();
    if (block.sapling.isSapling()) {
        // A sapling without its timer (set by a command) gets one first.
        if (!m_state.blockEntities().sapling(target)) {
            m_state.plants().saplingPlaced(m_state.blockEntities(), target, *clicked);
        }
        if (m_state.blockEntities().sapling(target) && random.generateDouble() < QcItemDefinition::fertilizeSaplingChance) {
            m_state.blockEntities().setSaplingRemaining(target, 0.0);
        }
    } else if (block.growsVegetation) {
        if (!growVegetation(target)) {
            return false;
        }
    } else if (!growCrop(target, random.bounded(fertilizeStagesMin, fertilizeStagesMax + 1))) {
        return false;
    }

    m_sounds.playBlock(block, QcBlockSoundAction::Place, target);
    if (!client.rules().infiniteItems) {
        client.inventory.removeFromSlot(client.inventory.selectedHotbarSlot(), 1);
    }
    m_inventory.send(client);
    return true;
}

bool QcServerPlants::growVegetation(QcBlockPos target)
{
    if (m_state.world().blockAt(target.offset(0, 1, 0)) != qcAirBlockId) {
        return false;
    }
    const QList<QcVegetationSpread::Planting> plantings = m_vegetation.spread(m_state.world(), target, m_state.random());
    for (const QcVegetationSpread::Planting &planting : plantings) {
        m_blocks.setAndBroadcast(planting.pos, planting.block);
    }
    return true;
}

bool QcServerPlants::growCrop(QcBlockPos pos, int stages)
{
    const QcContentRegistry &content = m_state.content();
    const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
    if (!block || !content.isValidBlock(*block) || !content.block(*block).crop.isCrop()) {
        return false;
    }

    const QcCropDefinition &crop = content.block(*block).crop;
    const QcBlockProperty *values = content.block(*block).property(crop.property);
    const qsizetype stage = values ? values->values.indexOf(content.property(*block, crop.property)) : -1;
    const qsizetype last = values ? values->values.size() - 1 : -1;
    if (stage < 0 || stage >= last) {
        return false;
    }
    m_blocks.setAndBroadcast(pos, content.withProperty(*block, crop.property, values->values[std::min(last, stage + stages)]));
    return true;
}
