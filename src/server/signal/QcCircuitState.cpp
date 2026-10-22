#include "server/signal/QcCircuitState.h"

#include "server/blocks/QcBlockRules.h"
#include "server/core/QcServerLogging.h"
#include "server/core/QcServerWorld.h"


#include <algorithm>
#include <array>
#include <tuple>

using namespace Qt::StringLiterals;

namespace {

// How far a change reaches into circuits: a component's input looks two blocks away at most (through
// the block a source powers), dust slopes one.
constexpr int reach = 2;

} // namespace

bool QcCircuitState::isNearCircuit(const QcServerWorld &world, QcBlockPos pos) const
{
    const QcSectionMarks &marks = world.marks();
    if (marks.isEmpty(QcBlockTrait::Circuit)) {
        return false;
    }

    // The sections from two below to two above, as bits.
    const auto low = unsigned(std::max(0, pos.y - reach) / QcWorld::sectionHeight);
    const auto high = unsigned(std::min(QcWorld::worldHeight - 1, pos.y + reach) / QcWorld::sectionHeight);
    const quint32 sections = ((2U << high) - 1U) & ~((1U << low) - 1U);
    for (const int dx : {-reach, reach}) {
        for (const int dz : {-reach, reach}) {
            if ((marks.sectionsOf(pos.offset(dx, 0, dz).chunk(), QcBlockTrait::Circuit) & sections) != 0) {
                return true;
            }
        }
    }
    return false;
}

void QcCircuitState::noteChange(const QcServerWorld &world, QcBlockPos pos, QcBlockId before, QcBlockId after)
{
    const QcContentRegistry &content = world.content();
    const auto kindOf = [&content](QcBlockId id) { return content.isValidBlock(id) ? content.block(id).signal.kind : QcSignalKind::None; };
    if (kindOf(before) == QcSignalKind::None && kindOf(after) == QcSignalKind::None && !isNearCircuit(world, pos)) {
        return;
    }

    if (changes.size() >= maxPendingChanges) {
        if (droppedChanges++ % droppedReportInterval == 0) {
            qCWarning(lcServer) << "Too many pending circuit changes; dropped" << droppedChanges << "so far";
        }
        return;
    }
    changes.append({pos, before, after});
}

void QcCircuitState::noteContents(const QcServerWorld &world, QcBlockPos pos)
{
    if (!isNearCircuit(world, pos)) {
        return;
    }
    const QList<QcBlockPos> found = readersOf(world, pos);
    for (const QcBlockPos &reader : found) {
        readers.insert(reader);
    }
}

QList<QcBlockPos> QcCircuitState::readersOf(const QcServerWorld &world, QcBlockPos pos)
{
    const QcContentRegistry &content = world.content();
    QList<QcBlockPos> found;
    for (const QcBlockFace face : {QcBlockFace::North, QcBlockFace::East, QcBlockFace::South, QcBlockFace::West}) {
        const QcBlockPos next = qcNeighbour(pos, face);
        for (const QcBlockPos candidate : {next, qcNeighbour(next, face)}) {
            const std::optional<QcBlockId> block = world.blockAt(candidate);
            const bool comparator = block && content.block(*block).signal.kind == QcSignalKind::Comparator;
            if (comparator && content.property(*block, u"facing"_s) == QcBlockRules::faceName(face)) {
                found.append(candidate);
            }
        }
    }
    return found;
}

void QcCircuitState::takeReaders(const QcServerWorld &world)
{
    QList<QcBlockPos> pending(readers.begin(), readers.end());
    readers.clear();
    std::ranges::sort(pending, [](QcBlockPos a, QcBlockPos b) { return std::tuple{a.y, a.z, a.x} < std::tuple{b.y, b.z, b.x}; });
    for (const QcBlockPos &pos : std::as_const(pending)) {
        const QcBlockId block = world.blockAt(pos).value_or(qcAirBlockId);
        changes.append({pos, block, block, true});
    }
}

bool QcCircuitState::isSurroundedByLoaded(const QcServerWorld &world, QcChunkPos chunk)
{
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            if (!world.chunk({chunk.x + dx, chunk.z + dz})) {
                return false;
            }
        }
    }
    return true;
}

void QcCircuitState::wake(const QcServerWorld &world, QcBlockPos pos)
{
    if (!isSurroundedByLoaded(world, pos.chunk())) {
        waiting[pos.chunk()].append(pos);
        return;
    }
    changes.append({pos, qcAirBlockId, qcAirBlockId, false, false});
}

void QcCircuitState::wakeBorder(const QcServerWorld &world, QcChunkPos chunk, QcBlockFace face)
{
    const quint32 sections = world.marks().sectionsOf(chunk, QcBlockTrait::Circuit);
    if (sections == 0) {
        return;
    }

    const QcFaceOffset offset = qcFaceOffset(face);
    const int fixed = offset.dx + offset.dz > 0 ? QcWorld::chunkSize - 1 : 0;
    for (int section = 0; section < QcWorld::sectionCount; ++section) {
        if ((sections & (1U << unsigned(section))) == 0) {
            continue;
        }
        for (int y = section * QcWorld::sectionHeight; y < (section + 1) * QcWorld::sectionHeight; ++y) {
            for (int along = 0; along < QcWorld::chunkSize; ++along) {
                const int x = chunk.x * QcWorld::chunkSize + (offset.dx != 0 ? fixed : along);
                const int z = chunk.z * QcWorld::chunkSize + (offset.dx != 0 ? along : fixed);
                wakeOne(world, {x, y, z});
            }
        }
    }
}

void QcCircuitState::wakeOne(const QcServerWorld &world, QcBlockPos pos)
{
    const QcBlockId block = world.blockAt(pos).value_or(qcAirBlockId);
    const QcSignalKind kind = world.content().block(block).signal.kind;
    if (kind != QcSignalKind::None && kind != QcSignalKind::Piston && kind != QcSignalKind::PistonHead) {
        changes.append({pos, block, block, true, false});
    }
}

void QcCircuitState::chunkLoaded(const QcServerWorld &world, QcChunkPos chunk)
{
    const QList<QcBlockPos> resumed = waitingOn.take(chunk);
    for (const QcBlockPos &pos : resumed) {
        // A signal that waited goes on as one; a look after a chunk came stays a look.
        changes.append({pos, qcAirBlockId, qcAirBlockId, false, signalWaits.remove(pos)});
    }

    constexpr std::array<QcBlockFace, 4> sides{QcBlockFace::North, QcBlockFace::East, QcBlockFace::South, QcBlockFace::West};
    for (const QcBlockFace side : sides) {
        const QcFaceOffset offset = qcFaceOffset(side);
        const QcChunkPos neighbour{chunk.x + offset.dx, chunk.z + offset.dz};
        if (world.chunk(neighbour)) {
            wakeBorder(world, chunk, side);
            wakeBorder(world, neighbour, QcBlockRules::opposite(side));
        }
    }

    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            const QcChunkPos around{chunk.x + dx, chunk.z + dz};
            if (waiting.contains(around) && isSurroundedByLoaded(world, around)) {
                releaseWaiting(around);
            }
        }
    }
}

void QcCircuitState::releaseWaiting(QcChunkPos chunk)
{
    const QList<QcBlockPos> positions = waiting.take(chunk);
    for (const QcBlockPos &pos : positions) {
        changes.append({pos, qcAirBlockId, qcAirBlockId, false, false});
    }
}

void QcCircuitState::forgetChunk(QcChunkPos chunk)
{
    waiting.remove(chunk);
    waitingOn.remove(chunk);
    const auto inChunk = [chunk](QcBlockPos pos) { return pos.chunk() == chunk; };
    changes.removeIf([&inChunk](const Change &change) { return inChunk(change.pos); });
}

void QcCircuitState::Pending::add(QcBlockPos pos, bool signal)
{
    (signal ? onTheirWay : looks).append(pos);
}

QcCircuitState::Pending QcCircuitState::takeChunk(QcChunkPos chunk)
{
    const auto inChunk = [chunk](QcBlockPos pos) { return pos.chunk() == chunk; };
    Pending taken;
    taken.looks = waiting.take(chunk);
    changes.removeIf([&](const Change &change) {
        if (!inChunk(change.pos)) {
            return false;
        }
        taken.add(change.pos, change.signal);
        return true;
    });
    for (auto it = waitingOn.begin(); it != waitingOn.end();) {
        it->removeIf([&](QcBlockPos pos) {
            if (!inChunk(pos)) {
                return false;
            }
            taken.add(pos, signalWaits.contains(pos));
            return true;
        });
        it = it->isEmpty() ? waitingOn.erase(it) : std::next(it);
    }
    readers.removeIf([&](QcBlockPos pos) {
        if (!inChunk(pos)) {
            return false;
        }
        taken.add(pos, true);
        return true;
    });
    signalWaits.removeIf(inChunk);
    return taken;
}

QHash<QcChunkPos, QcCircuitState::Pending> QcCircuitState::pendingByChunk() const
{
    QHash<QcChunkPos, Pending> pending;
    for (const Change &change : changes) {
        pending[change.pos.chunk()].add(change.pos, change.signal);
    }
    for (const QList<QcBlockPos> &positions : waiting) {
        for (const QcBlockPos &pos : positions) {
            pending[pos.chunk()].add(pos, false);
        }
    }
    for (const QList<QcBlockPos> &positions : waitingOn) {
        for (const QcBlockPos &pos : positions) {
            pending[pos.chunk()].add(pos, signalWaits.contains(pos));
        }
    }
    return pending;
}

void QcCircuitState::clear()
{
    changes.clear();
    waiting.clear();
    waitingOn.clear();
    readers.clear();
}
