#include "clientcore/session/QcCampfireSmoke.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

// A number in [0, 1) for puff round `round` of puff `index` of the campfire at `pos`.
double unitOf(QcBlockPos pos, int index, qint64 round, quint32 salt)
{
    quint64 h = static_cast<quint64>(static_cast<qint64>(pos.x)) * 0x9E3779B97F4A7C15ull
        ^ static_cast<quint64>(static_cast<qint64>(pos.y)) * 0xC2B2AE3D27D4EB4Full
                ^ static_cast<quint64>(static_cast<qint64>(pos.z)) * 0x165667B19E3779F9ull;
    h += static_cast<quint64>(index) * 0x27D4EB2F165667C5ull + static_cast<quint64>(round) * 0x94D049BB133111EBull + salt;
    h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9ull;
    h = (h ^ (h >> 27)) * 0x94D049BB133111EBull;
    h ^= h >> 31;
    return static_cast<double>(h >> 11) / static_cast<double>(1ull << 53);
}

} // namespace

void QcCampfireSmoke::refresh(const QcClientWorld &world, const QcVec3 &eye, quint64 tick)
{
    if (m_content != &world.content()) {
        learn(world.content());
        m_scanned = false;
    }
    if (m_scanned && tick < m_scannedAt + rescanTicks && tick >= m_scannedAt) {
        return;
    }
    m_campfires = scan(world, eye);
    m_scannedAt = tick;
    m_scanned = true;
}

void QcCampfireSmoke::learn(const QcContentRegistry &content)
{
    m_content = &content;
    m_smokes.assign(static_cast<std::size_t>(content.stateCount()), false);
    for (std::size_t id = 0; id < m_smokes.size(); ++id) {
        m_smokes[id] = content.block(static_cast<QcBlockId>(id)).campfire.cooks();
    }
}

bool QcCampfireSmoke::smokes(QcBlockId id) const
{
    return static_cast<std::size_t>(id) < m_smokes.size() && m_smokes[static_cast<std::size_t>(id)];
}

QList<QcBlockPos> QcCampfireSmoke::campfiresIn(const QcChunkSection &section, QcBlockPos origin) const
{
    QList<QcBlockPos> found;
    if (!std::ranges::any_of(section.palette(), [this](QcBlockId id) { return smokes(id); })) {
        return found;
    }

    for (int cell = 0; cell < QcWorld::chunkSize * QcWorld::chunkSize * QcWorld::sectionHeight; ++cell) {
        const int x = cell % QcWorld::chunkSize;
        const int z = cell / QcWorld::chunkSize % QcWorld::chunkSize;
        const int y = cell / (QcWorld::chunkSize * QcWorld::chunkSize);
        if (smokes(section.block(x, y, z))) {
            found.append({origin.x + x, origin.y + y, origin.z + z});
        }
    }
    return found;
}

QList<QcBlockPos> QcCampfireSmoke::scan(const QcClientWorld &world, const QcVec3 &eye) const
{
    const QcBlockPos centre{static_cast<int>(std::floor(eye.x)), static_cast<int>(std::floor(eye.y)), static_cast<int>(std::floor(eye.z))};
    const int lowest = std::max(0, (centre.y - radius) / QcWorld::sectionHeight);
    const int highest = std::min(QcWorld::sectionCount - 1, (centre.y + radius) / QcWorld::sectionHeight);
    QList<QcBlockPos> found;
    for (int cx = (centre.x - radius) >> 4; cx <= (centre.x + radius) >> 4; ++cx) {
        for (int cz = (centre.z - radius) >> 4; cz <= (centre.z + radius) >> 4; ++cz) {
            const std::shared_ptr<const QcChunk> chunk = world.chunk({cx, cz});
            for (int index = lowest; chunk && index <= highest; ++index) {
                found.append(campfiresIn(chunk->section(index), {cx * QcWorld::chunkSize, index * QcWorld::sectionHeight, cz * QcWorld::chunkSize}));
            }
        }
    }

    // The nearest first, at most maxCampfires.
    const auto distance = [&eye](QcBlockPos pos) { return (QcVec3{pos.x + 0.5, pos.y + 0.5, pos.z + 0.5} - eye).length(); };
    std::ranges::sort(found, {}, distance);
    found.resize(std::min<qsizetype>(found.size(), maxCampfires));
    return found;
}

QList<QcCampfireSmoke::Puff> QcCampfireSmoke::puffsOf(QcBlockPos pos, double seconds)
{
    QList<Puff> puffs;
    for (int index = 0; index < puffsPerCampfire; ++index) {
        // Each puff starts its loop at its own time; every loop drifts its own way.
        const double time = seconds + index * puffSeconds / puffsPerCampfire;
        const auto round = static_cast<qint64>(std::floor(time / puffSeconds));
        const double age = time / puffSeconds - static_cast<double>(round);
        const double driftX = (unitOf(pos, index, round, 1) - 0.5) * 0.8;
        const double driftZ = (unitOf(pos, index, round, 2) - 0.5) * 0.8;
        const double rise = 0.45 + age * 3.2;
        puffs.append({{pos.x + 0.5 + driftX * age, pos.y + rise, pos.z + 0.5 + driftZ * age}, 0.18 + 0.22 * std::sin(age * std::numbers::pi)});
    }
    return puffs;
}
