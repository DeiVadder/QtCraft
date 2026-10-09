#include "QcPlantManager.h"

#include <QLoggingCategory>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

Q_LOGGING_CATEGORY(lcPlants, "qtcraft.plants")

} // namespace

QcPlantManager::QcPlantManager(std::shared_ptr<const QcContentRegistry> content, quint64 seed)
    : m_content(std::move(content))
    , m_seed(seed)
    , m_random(quint32(seed ^ (seed >> 32) ^ 0x5A91u))
{
    QHash<QString, QcTreeShape::Params> features;
    for (const QcFeatureDefinition &feature : m_content->features()) {
        QcTreeShape::Params params;
        params.type = feature.type;
        params.trunk = m_content->blockId(feature.trunkBlock).value_or(qcAirBlockId);
        params.leaves = m_content->blockId(feature.leavesBlock).value_or(qcAirBlockId);
        params.minHeight = feature.minHeight;
        params.maxHeight = feature.maxHeight;
        params.cone = feature.shape == u"cone"_s;

        if (params.trunk == qcAirBlockId || feature.type == QcFeatureDefinition::Type::Pool) {
            continue;
        }

        features.insert(feature.name, params);
        if (feature.type == QcFeatureDefinition::Type::Tree && params.leaves != qcAirBlockId) {
            m_trunks.insert(params.trunk);
            m_leaves.insert(params.leaves);
        }
    }

    const QList<QcBlockDefinition> &blocks = m_content->blocks();
    for (qsizetype i = 0; i < blocks.size(); ++i) {
        const QcSaplingDefinition &sapling = blocks[i].sapling;
        if (!sapling.isSapling()) {
            continue;
        }

        const QcBlockId block = m_content->stateTable().firstState(quint32(i));
        QSet<QcBlockId> ground;
        for (const QString &name : sapling.plantOn) {
            if (const std::optional<QcBlockId> id = m_content->blockId(name)) {
                ground.insert(*id);
            }
        }

        m_plantOn.insert(block, ground);

        const auto feature = features.constFind(sapling.feature);
        if (feature == features.constEnd()) {
            qCWarning(lcPlants) << "Sapling" << blocks[i].name << "names the unknown feature" << sapling.feature << "and never grows";
            continue;
        }

        m_trees.insert(block, *feature);
    }
}

bool QcPlantManager::canPlant(QcBlockId sapling, std::optional<QcBlockId> ground) const
{
    const auto allowed = m_plantOn.constFind(sapling);
    if (allowed == m_plantOn.constEnd() || !ground || !m_content->isValidBlock(*ground)) {
        return false;
    }

    return allowed->isEmpty() ? m_content->block(*ground).solid : allowed->contains(*ground);
}

void QcPlantManager::blockRemoved(QcBlockPos pos, QcBlockId previous, const QcBlockAccess &world)
{
    if (!isTrunk(previous)) {
        return;
    }

    for (int dx = -leafReach; dx <= leafReach; ++dx) {
        for (int dy = -leafReach; dy <= leafReach; ++dy) {
            for (int dz = -leafReach; dz <= leafReach; ++dz) {
                planDecayCheck(pos.offset(dx, dy, dz), world);
            }
        }
    }
}

void QcPlantManager::planDecayCheck(QcBlockPos pos, const QcBlockAccess &world)
{
    const std::optional<QcBlockId> block = world.blockAt(pos);
    if (!block || !isLeaves(*block) || m_pendingPositions.contains(pos) || m_pending.size() >= maxPendingChecks) {
        return;
    }

    const double delay = minDecayDelay + m_random.bounded(maxDecayDelay - minDecayDelay);
    m_pending.append({pos, m_time + delay});
    m_pendingPositions.insert(pos);
}

void QcPlantManager::saplingPlaced(QcBlockPos pos, QcBlockId sapling)
{
    if (!m_content->isValidBlock(sapling) || !m_content->block(sapling).sapling.isSapling()) {
        return;
    }

    if (m_saplings.size() >= maxSaplings) {
        qCWarning(lcPlants) << "Too many growing saplings; the one at" << pos.x << pos.y << pos.z << "will not grow";
        return;
    }

    const QcSaplingDefinition &definition = m_content->block(sapling).sapling;
    const double span = definition.maxSeconds - definition.minSeconds;
    m_saplings.append({pos, definition.minSeconds + (span > 0.0 ? m_random.bounded(span) : 0.0)});
}

QcPlantManager::Support QcPlantManager::supportOf(QcBlockPos pos, const QcBlockAccess &world) const
{
    bool unknown = false;
    for (int dx = -leafReach; dx <= leafReach; ++dx) {
        for (int dy = -leafReach; dy <= leafReach; ++dy) {
            if (rowHasTrunk(pos.offset(dx, dy, 0), world, unknown)) {
                return Support::Trunk;
            }
        }
    }
    return unknown ? Support::Unknown : Support::None;
}

bool QcPlantManager::rowHasTrunk(QcBlockPos centre, const QcBlockAccess &world, bool &unknown) const
{
    for (int dz = -leafReach; dz <= leafReach; ++dz) {
        const std::optional<QcBlockId> block = world.blockAt(centre.offset(0, 0, dz));
        if (!block) {
            unknown = true;
        } else if (isTrunk(*block)) {
            return true;
        }
    }
    return false;
}

bool QcPlantManager::grow(const Sapling &sapling, QcBlockId block, const QcBlockAccess &world, const SetBlock &setBlock)
{
    const QcTreeShape::Params &tree = m_trees[block];
    const double random = m_random.generateDouble();

    struct Placement
    {
        QcBlockPos pos;
        QcBlockId block;
        bool onlyIntoAir;
    };

    QList<Placement> placements;
    QcTreeShape::build(tree, sapling.pos, random, m_seed, [&placements](QcBlockPos pos, QcBlockId id, bool onlyIntoAir) {
        placements.append({pos, id, onlyIntoAir});
    });

    // The trunk needs free space (or the sapling itself); leaves fill only what is free.
    for (const Placement &placement : std::as_const(placements)) {
        const std::optional<QcBlockId> existing = world.blockAt(placement.pos);
        if (!existing || !placement.pos.isInsideWorldHeight()) {
            return false;
        }

        const bool free = *existing == qcAirBlockId || placement.pos == sapling.pos
                          || (m_content->block(*existing).replaceable && !m_content->block(*existing).liquid);
        if (!placement.onlyIntoAir && !free) {
            return false;
        }
    }

    for (const Placement &placement : std::as_const(placements)) {
        const std::optional<QcBlockId> existing = world.blockAt(placement.pos);
        if (placement.onlyIntoAir && existing != qcAirBlockId) {
            continue;
        }
        setBlock(placement.pos, placement.block);
    }
    return true;
}

void QcPlantManager::tick(const QcBlockAccess &world, double dt, const LoadedFunction &isLoaded, const SetBlock &setBlock,
                          const BreakBlock &breakBlock)
{
    m_time += dt;
    int decays = 0;
    int checks = 0;

    for (qsizetype i = 0; i < m_pending.size() && decays < maxDecaysPerTick && checks < maxChecksPerTick;) {
        const PendingCheck &check = m_pending[i];
        if (check.due > m_time) {
            ++i;
            continue;
        }

        ++checks;
        const std::optional<QcBlockId> block = world.blockAt(check.pos);
        const Support support = block && isLeaves(*block) ? supportOf(check.pos, world) : Support::Trunk;

        if (support == Support::Unknown) {
            // Wait until the neighbourhood is loaded rather than guessing; moved to the end so
            // waiting checks do not keep the others from their turn.
            PendingCheck later = check;
            later.due = m_time + recheckSeconds;
            m_pending.removeAt(i);
            m_pending.append(later);
            continue;
        }

        const QcBlockPos pos = check.pos;
        m_pending.removeAt(i);
        m_pendingPositions.remove(pos);

        if (support == Support::None) {
            breakBlock(pos);
            ++decays;
        }
    }

    QList<Sapling> waiting;
    for (Sapling sapling : std::as_const(m_saplings)) {
        if (!isLoaded(sapling.pos)) {
            waiting.append(sapling);
            continue;
        }

        const std::optional<QcBlockId> block = world.blockAt(sapling.pos);
        if (!block || !m_content->isValidBlock(*block) || !m_content->block(*block).sapling.isSapling()) {
            // Broken or replaced meanwhile.
            continue;
        }

        sapling.remaining -= dt;
        if (sapling.remaining > 0.0) {
            waiting.append(sapling);
            continue;
        }

        if (!m_trees.contains(*block)) {
            continue;
        }

        if (!grow(sapling, *block, world, setBlock)) {
            sapling.remaining = retrySeconds;
            waiting.append(sapling);
        }
    }

    m_saplings = std::move(waiting);
}

void QcPlantManager::restoreSaplings(const QList<Sapling> &saplings)
{
    m_saplings = saplings.first(std::min<qsizetype>(saplings.size(), maxSaplings));
}
