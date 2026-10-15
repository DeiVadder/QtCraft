#include "QcServerPlants.h"

#include "QcBlockRules.h"
#include "QcLineOfSight.h"
#include "QcPhysics.h"
#include "QcServerBlocks.h"
#include "QcServerInventory.h"
#include "QcServerSounds.h"
#include "QcServerState.h"

#include <array>

QcServerPlants::QcServerPlants(QcServerState &state, QcServerSounds &sounds, QcServerBlocks &blocks, QcServerInventory &inventory)
    : m_state(state)
    , m_sounds(sounds)
    , m_blocks(blocks)
    , m_inventory(inventory)
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

