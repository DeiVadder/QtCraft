#include "QcVillages.h"

#include "QcVillagePlan.h"
#include "QcWorldGenerator.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace {

bool inChunk(QcBlockPos pos, QcChunkPos chunk)
{
    return pos.chunk() == chunk;
}

// A villager's claim stays on a spot that is still there.
void keepClaims(QList<QcVillage::Spot> &now, const QList<QcVillage::Spot> &before)
{
    for (QcVillage::Spot &spot : now) {
        const auto it = std::ranges::find_if(before, [&spot](const QcVillage::Spot &old) { return old.at == spot.at; });
        if (it != before.cend()) {
            spot.claimedBy = it->claimedBy;
        }
    }
}

} // namespace

QcVillages::QcVillages(const QcContentRegistry &content, const QcWorldGenerator &generator)
    : m_generator(generator)
    , m_blocks(content, nullptr)
{
}

const QcVillage *QcVillages::find(qint64 id) const
{
    const auto it = m_villages.constFind(id);
    return it != m_villages.cend() ? &*it : nullptr;
}

QcVillage *QcVillages::find(qint64 id)
{
    const auto it = m_villages.find(id);
    return it != m_villages.end() ? &*it : nullptr;
}

const QcVillage *QcVillages::at(QcBlockPos pos) const
{
    const QList<qint64> ids = m_byChunk.value(pos.chunk());
    for (const qint64 id : ids) {
        const QcVillage *village = find(id);
        if (village && village->contains(pos)) {
            return village;
        }
    }
    return nullptr;
}

QcVillage *QcVillages::villageAt(QcBlockPos pos)
{
    const QcVillage *village = std::as_const(*this).at(pos);
    return village ? find(village->id) : nullptr;
}

const QcVillage *QcVillages::nearest(QcBlockPos pos, double reach) const
{
    const QcVillage *best = nullptr;
    double bestDistance = reach;
    for (const QcVillage &village : m_villages) {
        const double distance = std::hypot(double(village.centre.x - pos.x), double(village.centre.z - pos.z));
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = &village;
        }
    }
    return best;
}

qsizetype QcVillages::population() const
{
    qsizetype total = 0;
    for (const QcVillage &village : m_villages) {
        total += village.population;
    }
    return total;
}

qsizetype QcVillages::golems(const QHash<qint64, int> &loaded) const
{
    qsizetype total = 0;
    for (const QcVillage &village : m_villages) {
        total += std::max(village.golems, loaded.value(village.id));
    }
    return total;
}

QList<QcVillage> QcVillages::villages() const
{
    std::map<qint64, QcVillage> ordered;
    for (const QcVillage &village : m_villages) {
        ordered.emplace(village.id, village);
    }
    QList<QcVillage> list;
    for (auto &[id, village] : ordered) {
        list.append(std::move(village));
    }
    return list;
}

void QcVillages::restore(const QList<QcVillage> &villages)
{
    m_villages.clear();
    m_byChunk.clear();
    for (const QcVillage &village : villages) {
        if (m_villages.size() < m_limits.villages && !m_villages.contains(village.id)) {
            add(village);
        }
    }
}

void QcVillages::add(const QcVillage &village)
{
    const QcChunkPos low = QcBlockPos{village.minX - QcVillage::reach, 0, village.minZ - QcVillage::reach}.chunk();
    const QcChunkPos high = QcBlockPos{village.maxX + QcVillage::reach, 0, village.maxZ + QcVillage::reach}.chunk();
    for (int cz = low.z; cz <= high.z; ++cz) {
        for (int cx = low.x; cx <= high.x; ++cx) {
            m_byChunk[{cx, cz}].append(village.id);
        }
    }
    m_villages.insert(village.id, village);
    ++m_changes;
}

QcVillage QcVillages::fromPlan(const QcVillagePlan &plan) const
{
    QcVillage village;
    village.id = plan.id;
    village.style = plan.style;
    village.centre = plan.centre;
    village.minX = plan.minX;
    village.minZ = plan.minZ;
    village.maxX = plan.maxX;
    village.maxZ = plan.maxZ;
    village.minY = plan.centre.y;
    village.maxY = plan.centre.y;
    for (const QcStructurePlacement &piece : plan.pieces) {
        village.minY = std::min(village.minY, piece.origin().y);
        village.maxY = std::max(village.maxY, piece.origin().y + piece.structure().sizeY() - 1);
    }
    for (qsizetype i = 0; i < std::min(plan.beds.size(), m_limits.beds); ++i) {
        village.beds.append({plan.beds[i].at, {}, {}});
    }
    for (qsizetype i = 0; i < std::min(plan.workstations.size(), m_limits.workstations); ++i) {
        village.workstations.append({plan.workstations[i].at, plan.workstations[i].kind, {}});
    }
    return village;
}

void QcVillages::noteChunk(const QcChunk &chunk)
{
    if (const std::shared_ptr<const QcVillagePlan> plan = m_generator.villageFor(chunk.pos());
        plan && plan->overlaps(chunk.pos(), 0) && !m_villages.contains(plan->id) && m_villages.size() < m_limits.villages) {
        add(fromPlan(*plan));
    }

    const QList<qint64> ids = m_byChunk.value(chunk.pos());
    for (const qint64 id : ids) {
        if (QcVillage *village = find(id)) {
            rescan(*village, chunk);
        }
    }
}

void QcVillages::rescan(QcVillage &village, const QcChunk &chunk)
{
    const QcChunkPos pos = chunk.pos();
    const QList<QcVillage::Spot> beds = village.beds;
    const QList<QcVillage::Spot> stations = village.workstations;
    const auto inside = [pos](const QcVillage::Spot &spot) { return inChunk(spot.at, pos); };
    village.beds.removeIf(inside);
    village.workstations.removeIf(inside);

    scan(village, chunk);
    keepClaims(village.beds, beds);
    keepClaims(village.workstations, stations);
    if (village.beds != beds || village.workstations != stations) {
        ++m_changes;
    }
}

void QcVillages::scan(QcVillage &village, const QcChunk &chunk)
{
    const QcChunkPos pos = chunk.pos();
    const int lowY = std::max(0, village.minY - QcVillage::reach);
    const int highY = std::min(QcWorld::worldHeight - 1, village.maxY + QcVillage::reach);
    for (int column = 0; column < QcWorld::chunkSize * QcWorld::chunkSize; ++column) {
        const int lx = column % QcWorld::chunkSize;
        const int lz = column / QcWorld::chunkSize;
        for (int y = lowY; y <= highY; ++y) {
            const QcBlockId block = chunk.block(lx, y, lz);
            const QcBlockPos at{pos.x * QcWorld::chunkSize + lx, y, pos.z * QcWorld::chunkSize + lz};
            if (m_blocks.matters(block) && village.contains(at)) {
                addSpot(village, at, block);
            }
        }
    }
}

void QcVillages::addSpot(QcVillage &village, QcBlockPos pos, QcBlockId block)
{
    if (m_blocks.isBedHead(block)) {
        if (village.beds.size() < m_limits.beds) {
            village.beds.append({pos, {}, {}});
        }
    } else if (village.workstations.size() < m_limits.workstations) {
        village.workstations.append({pos, m_blocks.workstationKind(block), {}});
    }
}

void QcVillages::noteBlock(QcBlockPos pos, QcBlockId before, QcBlockId after)
{
    if ((!m_blocks.matters(before) && !m_blocks.matters(after)) || m_byChunk.isEmpty()) {
        return;
    }
    QcVillage *village = villageAt(pos);
    if (!village) {
        return;
    }

    // A workstation turned in place keeps its villager.
    const auto at = [pos](const QcVillage::Spot &spot) { return spot.at == pos; };
    const auto old = std::ranges::find_if(village->workstations, at);
    const QString claimedBy = old != village->workstations.end() && old->kind == m_blocks.workstationKind(after) ? old->claimedBy : QString();
    const qsizetype removed = village->beds.removeIf(at) + village->workstations.removeIf(at);
    if (m_blocks.matters(after)) {
        addSpot(*village, pos, after);
        if (!m_blocks.isBedHead(after) && !village->workstations.isEmpty() && village->workstations.back().at == pos) {
            village->workstations.back().claimedBy = claimedBy;
        }
    }
    if (removed > 0 || m_blocks.matters(after)) {
        ++m_changes;
    }
}
