#include "clientcore/session/QcEnchantingAura.h"

#include "clientcore/textures/QcRuneGlyphs.h"
#include "common/gameplay/QcEnchantingTable.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

// A well mixed 64-bit value of a position, an index and a round (splitmix64's finaliser).
quint64 hashOf(QcBlockPos pos, qint64 round, quint32 salt)
{
    quint64 h = static_cast<quint64>(static_cast<qint64>(pos.x)) * 0x9E3779B97F4A7C15ull
        ^ static_cast<quint64>(static_cast<qint64>(pos.y)) * 0xC2B2AE3D27D4EB4Full
        ^ static_cast<quint64>(static_cast<qint64>(pos.z)) * 0x165667B19E3779F9ull;
    h += static_cast<quint64>(round) * 0x94D049BB133111EBull + salt;
    h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9ull;
    h = (h ^ (h >> 27)) * 0x94D049BB133111EBull;
    return h ^ (h >> 31);
}

// A number in [0, 1) for `pos`, `round` and `salt`.
double unitOf(QcBlockPos pos, qint64 round, quint32 salt)
{
    return static_cast<double>(hashOf(pos, round, salt) >> 11) / static_cast<double>(1ull << 53);
}

// The height of the book's spine above the table's feet.
constexpr double bookHeight = 1.15;

} // namespace

void QcEnchantingAura::refresh(const QcClientWorld &world, const QcVec3 &eye, quint64 tick)
{
    if (m_content != &world.content()) {
        learn(world.content());
        m_scanned = false;
    }
    if (m_scanned && tick < m_scannedAt + rescanTicks && tick >= m_scannedAt) {
        return;
    }
    m_tables = scan(world, eye);
    m_scannedAt = tick;
    m_scanned = true;
}

void QcEnchantingAura::learn(const QcContentRegistry &content)
{
    m_content = &content;
    m_isTable.assign(static_cast<std::size_t>(content.stateCount()), false);
    for (std::size_t id = 0; id < m_isTable.size(); ++id) {
        m_isTable[id] = content.block(static_cast<QcBlockId>(id)).opensEnchanting;
    }
}

bool QcEnchantingAura::isTable(QcBlockId id) const
{
    return static_cast<std::size_t>(id) < m_isTable.size() && m_isTable[static_cast<std::size_t>(id)];
}

QList<QcBlockPos> QcEnchantingAura::tablesIn(const QcChunkSection &section, QcBlockPos origin) const
{
    QList<QcBlockPos> found;
    if (!std::ranges::any_of(section.palette(), [this](QcBlockId id) { return isTable(id); })) {
        return found;
    }

    for (int cell = 0; cell < QcWorld::chunkSize * QcWorld::chunkSize * QcWorld::sectionHeight; ++cell) {
        const int x = cell % QcWorld::chunkSize;
        const int z = cell / QcWorld::chunkSize % QcWorld::chunkSize;
        const int y = cell / (QcWorld::chunkSize * QcWorld::chunkSize);
        if (isTable(section.block(x, y, z))) {
            found.append({origin.x + x, origin.y + y, origin.z + z});
        }
    }
    return found;
}

QList<QcEnchantingAura::Table> QcEnchantingAura::scan(const QcClientWorld &world, const QcVec3 &eye) const
{
    const QcBlockPos centre{static_cast<int>(std::floor(eye.x)), static_cast<int>(std::floor(eye.y)), static_cast<int>(std::floor(eye.z))};
    const int lowest = std::max(0, (centre.y - radius) / QcWorld::sectionHeight);
    const int highest = std::min(QcWorld::sectionCount - 1, (centre.y + radius) / QcWorld::sectionHeight);
    const QcChunkPos first = QcBlockPos{centre.x - radius, 0, centre.z - radius}.chunk();
    const QcChunkPos last = QcBlockPos{centre.x + radius, 0, centre.z + radius}.chunk();
    QList<QcBlockPos> found;
    for (int cx = first.x; cx <= last.x; ++cx) {
        for (int cz = first.z; cz <= last.z; ++cz) {
            const std::shared_ptr<const QcChunk> chunk = world.chunk({cx, cz});
            for (int index = lowest; chunk && index <= highest; ++index) {
                found.append(tablesIn(chunk->section(index), {cx * QcWorld::chunkSize, index * QcWorld::sectionHeight, cz * QcWorld::chunkSize}));
            }
        }
    }

    // The nearest first, at most maxTables, each with the shelves that count as the server counts them.
    const auto distance = [&eye](QcBlockPos pos) { return (QcVec3{pos.x + 0.5, pos.y + 0.5, pos.z + 0.5} - eye).length(); };
    std::ranges::sort(found, {}, distance);
    found.resize(std::min<qsizetype>(found.size(), maxTables));
    const auto blockAt = [&world](QcBlockPos at) { return world.blockAt(at); };
    QList<Table> tables;
    for (const QcBlockPos pos : std::as_const(found)) {
        tables.append({pos, QcEnchantingTable::shelvesIn(world.content(), pos, blockAt)});
    }
    return tables;
}

const QList<QcEnchantingAura::Rune> &QcEnchantingAura::runesOf(const Table &table, double seconds)
{
    QList<Rune> &runes = m_runes;
    runes.clear();
    const QcVec3 target{table.pos.x + 0.5, table.pos.y + bookHeight, table.pos.z + 0.5};
    for (const QcBlockPos shelf : table.shelves) {
        // Each shelf sends a rune once per cycle, at its own time; most of the cycle it rests.
        const double time = seconds + unitOf(shelf, 0, 1) * cycleSeconds;
        const auto round = static_cast<qint64>(std::floor(time / cycleSeconds));
        const double age = (time - static_cast<double>(round) * cycleSeconds) / flightSeconds;
        if (age >= 1.0) {
            continue;
        }
        const QcVec3 start{shelf.x + 0.2 + 0.6 * unitOf(shelf, round, 2), shelf.y + 0.4 + 0.8 * unitOf(shelf, round, 3),
                           shelf.z + 0.2 + 0.6 * unitOf(shelf, round, 4)};
        // Slow at the shelf, faster towards the table, in an arc above the straight line.
        const double along = age * age;
        const QcVec3 position = start + (target - start) * along + QcVec3{0.0, 0.7 * std::sin(age * std::numbers::pi), 0.0};
        const auto rune = static_cast<int>(hashOf(shelf, round, 5) % QcRuneGlyphs::count);
        runes.append({position, rune, 0.16 * (1.0 - 0.6 * along)});
    }
    return runes;
}

QcEnchantingAura::Book QcEnchantingAura::bookOf(QcBlockPos table, const QcVec3 &viewer, double seconds)
{
    const QcVec3 spine{table.x + 0.5, table.y + bookHeight + 0.05 * std::sin(seconds * 1.7), table.z + 0.5};
    const double dx = viewer.x - spine.x;
    const double dz = viewer.z - spine.z;
    const double distance = std::sqrt(dx * dx + dz * dz);
    // Facing the viewer, swaying a little; open while the viewer is near.
    const double yaw = std::atan2(dx, dz) + 0.12 * std::sin(seconds * 0.9);
    const double open = std::clamp((openDistance - distance) / (openDistance / 2.0), 0.0, 1.0);
    return {spine, yaw, open};
}
