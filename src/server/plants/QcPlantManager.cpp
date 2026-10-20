#include "server/plants/QcPlantManager.h"

#include "server/core/QcServerLogging.h"


#include <algorithm>

using namespace Qt::StringLiterals;

QcPlantManager::QcPlantManager(std::shared_ptr<const QcContentRegistry> content, quint64 seed)
    : m_content(std::move(content))
    , m_seed(seed)
    , m_random(quint32(seed ^ (seed >> 32) ^ 0x5A91u))
{
    QHash<QString, QcTreeShape::Params> features;
    for (const QcFeatureDefinition &feature : m_content->worldgen().features()) {
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
            // The trunk keeps its leaves also lying or stripped (decisions E-751, E-753).
            addTrunk(params.trunk);
            if (const std::optional<QcBlockId> stripped = m_content->blockId(m_content->block(params.trunk).strippedInto)) {
                addTrunk(*stripped);
            }
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

void QcPlantManager::addTrunk(QcBlockId block)
{
    // Every state of the block counts: logs lie along any axis.
    const QcBlockId first = m_content->stateTable().firstState(m_content->state(block).type);
    const qsizetype states = QcBlockStateTable::stateCountOf(m_content->block(block));
    for (qsizetype state = 0; state < states; ++state) {
        m_trunks.insert(first + QcBlockId(state));
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
    if (!block || !isLeaves(*block) || m_pendingPositions.contains(pos) || m_pending.size() >= m_pendingLimit) {
        return;
    }

    const double delay = minDecayDelay + m_random.bounded(maxDecayDelay - minDecayDelay);
    m_pending.append({pos, m_time + delay});
    m_pendingPositions.insert(pos);
}

void QcPlantManager::setLeafLimits(qint64 decays, qint64 checks, qint64 pending)
{
    m_decaysPerTick = decays;
    m_checksPerTick = checks;
    m_pendingLimit = pending;
}

void QcPlantManager::saplingPlaced(QcBlockEntities &entities, QcBlockPos pos, QcBlockId sapling)
{
    if (!m_content->isValidBlock(sapling) || !m_content->block(sapling).sapling.isSapling()) {
        return;
    }

    if (entities.saplingCount() >= entities.limits().saplings) {
        qCWarning(lcPlants) << "Too many growing saplings; the one at" << pos.x << pos.y << pos.z << "will not grow";
        return;
    }

    const QcSaplingDefinition &definition = m_content->block(sapling).sapling;
    const double span = definition.maxSeconds - definition.minSeconds;
    entities.plantSapling(pos, definition.minSeconds + (span > 0.0 ? m_random.bounded(span) : 0.0));
}

QcPlantManager::Support QcPlantManager::supportOf(QcBlockPos pos, const QcBlockAccess &world) const
{
    bool unknown = false;
    for (int dx = -leafReach; dx <= leafReach; ++dx) {
        for (int dy = -leafReach; dy <= leafReach; ++dy) {
            const Support row = rowSupport(pos.offset(dx, dy, 0), world);
            if (row == Support::Trunk) {
                return Support::Trunk;
            }
            unknown = unknown || row == Support::Unknown;
        }
    }
    return unknown ? Support::Unknown : Support::None;
}

QcPlantManager::Support QcPlantManager::rowSupport(QcBlockPos centre, const QcBlockAccess &world) const
{
    bool unknown = false;
    for (int dz = -leafReach; dz <= leafReach; ++dz) {
        const std::optional<QcBlockId> block = world.blockAt(centre.offset(0, 0, dz));
        if (!block) {
            unknown = true;
        } else if (isTrunk(*block)) {
            return Support::Trunk;
        }
    }
    return unknown ? Support::Unknown : Support::None;
}

bool QcPlantManager::grow(QcBlockPos sapling, QcBlockId block, const QcBlockAccess &world, const SetBlock &setBlock)
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
    QcTreeShape::build(tree, sapling, random, m_seed, [&placements](QcBlockPos pos, QcBlockId id, bool onlyIntoAir) {
        placements.append({pos, id, onlyIntoAir});
    });

    // The trunk needs free space (or the sapling itself); leaves fill only what is free.
    for (const Placement &placement : std::as_const(placements)) {
        const std::optional<QcBlockId> existing = world.blockAt(placement.pos);
        if (!existing || !placement.pos.isInsideWorldHeight()) {
            return false;
        }

        const bool free = *existing == qcAirBlockId || placement.pos == sapling
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

void QcPlantManager::tick(const QcBlockAccess &world, QcBlockEntities &entities, double dt, const Actions &actions)
{
    m_time += dt;
    decayLeaves(world, actions.breakBlock);
    growSaplings(world, entities, dt, actions.isLoaded, actions.setBlock);
}

void QcPlantManager::decayLeaves(const QcBlockAccess &world, const BreakBlock &breakBlock)
{
    int decays = 0;
    int checks = 0;
    for (qsizetype i = 0; i < m_pending.size() && decays < m_decaysPerTick && checks < m_checksPerTick;) {
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
}

void QcPlantManager::growSaplings(const QcBlockAccess &world, QcBlockEntities &entities, double dt, const LoadedFunction &isLoaded,
                                  const SetBlock &setBlock)
{
    using Step = QcBlockEntities::SaplingStep;
    entities.tickSaplings([&](QcBlockEntities::Sapling &sapling) {
        if (!isLoaded(sapling.pos)) {
            return Step::Keep;
        }

        const std::optional<QcBlockId> block = world.blockAt(sapling.pos);
        if (!block || !m_content->isValidBlock(*block) || !m_content->block(*block).sapling.isSapling()) {
            return Step::Remove;
        }

        sapling.remaining -= dt;
        if (sapling.remaining > 0.0) {
            return Step::Keep;
        }
        if (!m_trees.contains(*block)) {
            return Step::Remove;
        }

        // A tree that grew may have taken the place of other saplings: the walk seeks again.
        if (grow(sapling.pos, *block, world, setBlock)) {
            return Step::Grew;
        }
        sapling.remaining = retrySeconds;
        return Step::Keep;
    });
}
