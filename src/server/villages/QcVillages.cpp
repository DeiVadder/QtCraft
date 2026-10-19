#include "server/villages/QcVillages.h"

#include "server/core/QcServerLogging.h"
#include "worldgen/generators/QcWorldGenerator.h"
#include "worldgen/villages/QcVillagePlan.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace {

bool inChunk(QcBlockPos pos, QcChunkPos chunk)
{
    return pos.chunk() == chunk;
}

// The spots `now` with the villagers' claims of `before` on those still there (moved in and out,
// no copy).
[[nodiscard]] QList<QcVillage::Spot> withClaims(QList<QcVillage::Spot> now, const QList<QcVillage::Spot> &before)
{
    for (QcVillage::Spot &spot : now) {
        const auto it = std::ranges::find_if(before, [&spot](const QcVillage::Spot &old) { return old.at == spot.at; });
        if (it != before.cend()) {
            spot.claimedBy = it->claimedBy;
        }
    }
    return now;
}

} // namespace

QcVillages::QcVillages(const QcContentRegistry &content, const QcWorldGenerator &generator)
    : m_generator(generator)
    , m_blocks(content)
{
    for (const QString &warning : m_blocks.warnings()) {
        qCWarning(lcContent).noquote() << warning;
    }
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
    // Areas of generated villages may overlap by their reach: the spot belongs to the village whose
    // box is nearest (horizontally), the lower id on a tie, so every spot has exactly one village.
    const QcVillage *best = nullptr;
    int bestDistance = 0;
    const QList<qint64> ids = m_byChunk.value(pos.chunk());
    for (const qint64 id : ids) {
        const QcVillage *village = find(id);
        if (!village || !village->contains(pos)) {
            continue;
        }
        const int distance = boxDistance(*village, pos);
        if (!best || distance < bestDistance || (distance == bestDistance && village->id < best->id)) {
            best = village;
            bestDistance = distance;
        }
    }
    return best;
}

int QcVillages::boxDistance(const QcVillage &village, QcBlockPos pos)
{
    const int dx = std::max({village.minX - pos.x, pos.x - village.maxX, 0});
    const int dz = std::max({village.minZ - pos.z, pos.z - village.maxZ, 0});
    return std::max(dx, dz);
}

bool QcVillages::clashes(Box box, qint64 except) const
{
    // The box with its reach against every other area: the chunks the box reaches into with twice
    // the reach hold every village whose area it would meet.
    const int around = 2 * QcVillage::reach;
    const QcChunkPos low = QcBlockPos{box.minX - around, 0, box.minZ - around}.chunk();
    const QcChunkPos high = QcBlockPos{box.maxX + around, 0, box.maxZ + around}.chunk();
    for (int cz = low.z; cz <= high.z; ++cz) {
        for (int cx = low.x; cx <= high.x; ++cx) {
            if (meets(m_byChunk.value({cx, cz}), box, except)) {
                return true;
            }
        }
    }
    return false;
}

bool QcVillages::meets(const QList<qint64> &ids, Box box, qint64 except) const
{
    const int around = 2 * QcVillage::reach;
    return std::ranges::any_of(ids, [&](qint64 id) {
        const QcVillage *other = id != except ? find(id) : nullptr;
        return other && box.minX - around <= other->maxX && box.maxX + around >= other->minX && box.minZ - around <= other->maxZ
               && box.maxZ + around >= other->minZ;
    });
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

qsizetype QcVillages::golems(const QHash<qint64, int> &loaded, const std::function<bool(const QcVillage &)> &ofKind) const
{
    qsizetype total = 0;
    for (const QcVillage &village : m_villages) {
        const int now = loaded.value(village.id);
        total += ofKind(village) ? std::max(village.golems, now) : now;
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
    m_held.clear();
    for (const QcVillage &village : villages) {
        if (m_villages.size() < m_limits.villages && !m_villages.contains(village.id)) {
            add(village);
        }
    }
}

void QcVillages::add(const QcVillage &village, const std::shared_ptr<const QcVillagePlan> &plan)
{
    index(village);
    m_villages.insert(village.id, village);
    ++m_changes;
    holdLoaded(village, plan);
}

void QcVillages::index(const QcVillage &village)
{
    const QcChunkPos low = QcBlockPos{village.minX - QcVillage::reach, 0, village.minZ - QcVillage::reach}.chunk();
    const QcChunkPos high = QcBlockPos{village.maxX + QcVillage::reach, 0, village.maxZ + QcVillage::reach}.chunk();
    for (int cz = low.z; cz <= high.z; ++cz) {
        for (int cx = low.x; cx <= high.x; ++cx) {
            m_byChunk[{cx, cz}].append(village.id);
        }
    }
}

void QcVillages::unindex(const QcVillage &village)
{
    const QcChunkPos low = QcBlockPos{village.minX - QcVillage::reach, 0, village.minZ - QcVillage::reach}.chunk();
    const QcChunkPos high = QcBlockPos{village.maxX + QcVillage::reach, 0, village.maxZ + QcVillage::reach}.chunk();
    for (int cz = low.z; cz <= high.z; ++cz) {
        for (int cx = low.x; cx <= high.x; ++cx) {
            const auto it = m_byChunk.find({cx, cz});
            if (it != m_byChunk.end() && it->removeAll(village.id) > 0 && it->isEmpty()) {
                m_byChunk.erase(it);
            }
        }
    }
}

void QcVillages::holdLoaded(const QcVillage &village, const std::shared_ptr<const QcVillagePlan> &plan)
{
    if (!m_loaded) {
        return;
    }

    const QcChunkPos low = QcBlockPos{village.minX - QcVillage::reach, 0, village.minZ - QcVillage::reach}.chunk();
    const QcChunkPos high = QcBlockPos{village.maxX + QcVillage::reach, 0, village.maxZ + QcVillage::reach}.chunk();
    for (int cz = low.z; cz <= high.z; ++cz) {
        for (int cx = low.x; cx <= high.x; ++cx) {
            if (m_loaded({cx, cz})) {
                hold(village, {cx, cz}, plan);
            }
        }
    }
}

QList<qint64> QcVillages::near(QcChunkPos pos, int chunks) const
{
    QList<qint64> ids;
    for (int dz = -chunks; dz <= chunks; ++dz) {
        for (int dx = -chunks; dx <= chunks; ++dx) {
            ids.append(m_byChunk.value({pos.x + dx, pos.z + dz}));
        }
    }
    std::ranges::sort(ids);
    const auto repeated = std::ranges::unique(ids);
    ids.erase(repeated.begin(), repeated.end());
    return ids;
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

std::shared_ptr<const QcVillagePlan> QcVillages::learn(QcChunkPos pos)
{
    const std::shared_ptr<const QcVillagePlan> plan = m_generator.villageFor(pos);
    if (plan && plan->overlaps(pos, 0) && !m_villages.contains(plan->id) && m_villages.size() < m_limits.villages) {
        add(fromPlan(*plan), plan);
    }
    return plan;
}

void QcVillages::noteChunk(const QcChunk &chunk)
{
    const std::shared_ptr<const QcVillagePlan> plan = learn(chunk.pos());

    const QList<qint64> ids = m_byChunk.value(chunk.pos());
    for (const qint64 id : ids) {
        if (QcVillage *village = find(id)) {
            hold(*village, chunk.pos(), plan);
            rescan(*village, chunk);
        }
    }
}

void QcVillages::hold(const QcVillage &village, QcChunkPos pos, const std::shared_ptr<const QcVillagePlan> &regionPlan)
{
    // A settlement has no plan to hold.
    if (village.settlement()) {
        return;
    }
    Held &held = m_held[village.id];
    if (!held.plan) {
        held.plan = regionPlan && regionPlan->id == village.id ? regionPlan : m_generator.villageFor(village.centre.chunk());
    }
    held.chunks.insert(pos);
}

void QcVillages::forgetChunk(QcChunkPos pos)
{
    const QList<qint64> ids = m_byChunk.value(pos);
    for (const qint64 id : ids) {
        const auto it = m_held.find(id);
        if (it != m_held.end() && it->chunks.remove(pos) && it->chunks.isEmpty()) {
            m_held.erase(it);
        }
    }
}

std::shared_ptr<const QcVillagePlan> QcVillages::planOf(qint64 id) const
{
    if (const auto it = m_held.constFind(id); it != m_held.cend() && it->plan) {
        return it->plan;
    }
    // A settlement has no plan; a generated village's plan comes from the region of its centre.
    const QcVillage *village = find(id);
    if (!village || village->settlement()) {
        return nullptr;
    }
    const std::shared_ptr<const QcVillagePlan> plan = m_generator.villageFor(village->centre.chunk());
    return plan && plan->id == id ? plan : nullptr;
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
    village.beds = withClaims(std::move(village.beds), beds);
    village.workstations = withClaims(std::move(village.workstations), stations);
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
            const QcBlockPos cell{pos.x * QcWorld::chunkSize + lx, y, pos.z * QcWorld::chunkSize + lz};
            // A spot in the area of another village too is that one's if its box is nearer (at()).
            if (m_blocks.matters(block) && village.contains(cell) && at(cell) == &village) {
                addSpot(village, cell, block);
            }
        }
    }
}

bool QcVillages::listed(const QcVillage &village, QcBlockPos pos, QcBlockId block) const
{
    if (!m_blocks.matters(block)) {
        return false;
    }
    if (m_blocks.isBedHead(block)) {
        return std::ranges::any_of(village.beds, [pos](const QcVillage::Spot &spot) { return spot.at == pos; });
    }
    const QString kind = m_blocks.workstationKind(block);
    return std::ranges::any_of(village.workstations, [pos, &kind](const QcVillage::Spot &spot) { return spot.at == pos && spot.kind == kind; });
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

void QcVillages::notePointOfInterest(QcBlockPos pos, QcBlockId block)
{
    if (!m_blocks.matters(block)) {
        return;
    }
    // The generated villages it may join become known first, whichever of their chunks entered.
    const int chunks = (joinDistance + QcVillage::reach + QcWorld::chunkSize - 1) / QcWorld::chunkSize;
    for (int dz = -chunks; dz <= chunks; ++dz) {
        for (int dx = -chunks; dx <= chunks; ++dx) {
            learn({pos.chunk().x + dx, pos.chunk().z + dz});
        }
    }
    noteBlock(pos, qcAirBlockId, block);
}

void QcVillages::noteBlock(QcBlockPos pos, QcBlockId before, QcBlockId after)
{
    if (!m_blocks.matters(before) && !m_blocks.matters(after)) {
        return;
    }
    // A bed or workstation away from every village area joins a village near it or founds a
    // settlement (decision E-490).
    QcVillage *village = villageAt(pos);
    if (!village && m_blocks.matters(after)) {
        village = homeFor(pos);
    }
    if (!village || listed(*village, pos, after)) {
        return;
    }

    // A workstation turned in place keeps its villager.
    const auto at = [pos](const QcVillage::Spot &spot) { return spot.at == pos; };
    const auto old = std::ranges::find_if(village->workstations, at);
    const quint64 claimedBy = old != village->workstations.end() && old->kind == m_blocks.workstationKind(after) ? old->claimedBy : 0;
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
    dropIfEmpty(village->id);
}

QcVillage *QcVillages::homeFor(QcBlockPos pos)
{
    // The village whose box lies nearest (horizontally, at most joinDistance) and may still grow
    // to hold `pos`; its area reaches at most reach + joinDistance blocks into the chunks around.
    // A village whose grown area would meet another's is passed over: every spot belongs to exactly
    // one village.
    QcVillage *best = nullptr;
    int bestDistance = joinDistance + 1;
    const QList<qint64> ids = near(pos.chunk(), (joinDistance - QcVillage::reach + QcWorld::chunkSize - 1) / QcWorld::chunkSize);
    for (const qint64 id : ids) {
        QcVillage *village = find(id);
        const Box grown{std::min(village->minX, pos.x), std::min(village->minZ, pos.z), std::max(village->maxX, pos.x), std::max(village->maxZ, pos.z)};
        const int distance = boxDistance(*village, pos);
        if (distance < bestDistance && grown.maxX - grown.minX < m_limits.extent && grown.maxZ - grown.minZ < m_limits.extent
            && !clashes(grown, id)) {
            best = village;
            bestDistance = distance;
        }
    }
    if (best) {
        grow(*best, pos);
        return best;
    }
    // A settlement neither beyond the limit nor where its area would meet another's.
    if (m_villages.size() >= m_limits.villages || clashes({pos.x, pos.z, pos.x, pos.z}, 0)) {
        return nullptr;
    }

    // A settlement: its first spot is its centre and its box; it takes the style of its first
    // villager (QcVillagerHomes::moveIn).
    QcVillage settlement;
    settlement.id = QcVillage::settlementId(pos);
    if (QcVillage *same = find(settlement.id)) {
        return same;
    }
    settlement.centre = pos;
    settlement.minX = settlement.maxX = pos.x;
    settlement.minY = settlement.maxY = pos.y;
    settlement.minZ = settlement.maxZ = pos.z;
    add(settlement);
    return find(settlement.id);
}

void QcVillages::grow(QcVillage &village, QcBlockPos pos)
{
    unindex(village);
    village.minX = std::min(village.minX, pos.x);
    village.minY = std::min(village.minY, pos.y);
    village.minZ = std::min(village.minZ, pos.z);
    village.maxX = std::max(village.maxX, pos.x);
    village.maxY = std::max(village.maxY, pos.y);
    village.maxZ = std::max(village.maxZ, pos.z);
    index(village);
    holdLoaded(village, nullptr);
    ++m_changes;
}

void QcVillages::dropIfEmpty(qint64 id)
{
    const QcVillage *village = find(id);
    if (!village || !village->settlement() || !village->beds.isEmpty() || !village->workstations.isEmpty() || village->population > 0) {
        return;
    }
    unindex(*village);
    m_held.remove(id);
    m_villages.remove(id);
    ++m_changes;
}
