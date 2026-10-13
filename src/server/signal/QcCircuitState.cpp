#include "QcCircuitState.h"

#include "QcBlockRules.h"
#include "QcMovingBlocks.h"
#include "QcServerWorld.h"

#include <QLoggingCategory>

#include <algorithm>
#include <array>

namespace {

Q_LOGGING_CATEGORY(lcCircuits, "qtcraft.server")

// How far a change reaches into circuits: a component's input looks two blocks away at most (through
// the block a source powers), dust slopes one.
constexpr int reach = 2;

} // namespace

bool QcCircuitState::isNearCircuit(const QcServerWorld &world, QcBlockPos pos) const
{
    const QcSectionMarks &marks = world.signalSections();
    if (marks.isEmpty()) {
        return false;
    }

    // The sections from two below to two above, as bits.
    const auto low = unsigned(std::max(0, pos.y - reach) / QcWorld::sectionHeight);
    const auto high = unsigned(std::min(QcWorld::worldHeight - 1, pos.y + reach) / QcWorld::sectionHeight);
    const quint32 sections = ((2U << high) - 1U) & ~((1U << low) - 1U);
    for (const int dx : {-reach, reach}) {
        for (const int dz : {-reach, reach}) {
            if ((marks.sectionsOf(pos.offset(dx, 0, dz).chunk()) & sections) != 0) {
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
    if (kindOf(before) != kindOf(after)) {
        forgetMemory(pos, kindOf(after));
    }
    if (kindOf(before) == QcSignalKind::None && kindOf(after) == QcSignalKind::None && !isNearCircuit(world, pos)) {
        return;
    }

    if (changes.size() >= maxPendingChanges) {
        if (droppedChanges++ % droppedReportInterval == 0) {
            qCWarning(lcCircuits) << "Too many pending circuit changes; dropped" << droppedChanges << "so far";
        }
        return;
    }
    changes.append({pos, before, after});
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

void QcCircuitState::forgetMemory(QcBlockPos pos, QcSignalKind now)
{
    if (now != QcSignalKind::Torch) {
        torchOffTicks.remove(pos);
        burntOutUntil.remove(pos);
    }
    if (now != QcSignalKind::Opener) {
        poweredOpeners.remove(pos);
    }
    if (now != QcSignalKind::Piston) {
        extending.remove(pos);
    }
}

void QcCircuitState::prune(quint64 now, quint64 window)
{
    torchOffTicks.removeIf([now, window](const auto &it) { return it.value().isEmpty() || it.value().constLast() + window <= now; });
    burntOutUntil.removeIf([now](const auto &it) { return it.value() <= now; });
    extending.removeIf([now](const auto &it) { return it.value() + QcMovingBlocks::moveTicks <= now; });
}

void QcCircuitState::wake(const QcServerWorld &world, QcBlockPos pos)
{
    if (!isSurroundedByLoaded(world, pos.chunk())) {
        waiting[pos.chunk()].append(pos);
        return;
    }
    changes.append({pos, qcAirBlockId, qcAirBlockId});
}

void QcCircuitState::wakeBorder(const QcServerWorld &world, QcChunkPos chunk, QcBlockFace face)
{
    const quint32 sections = world.signalSections().sectionsOf(chunk);
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
        changes.append({pos, block, block, true});
    }
}

void QcCircuitState::chunkLoaded(const QcServerWorld &world, QcChunkPos chunk)
{
    const QList<QcBlockPos> readers = waitingOn.take(chunk);
    for (const QcBlockPos &pos : readers) {
        changes.append({pos, qcAirBlockId, qcAirBlockId});
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
        changes.append({pos, qcAirBlockId, qcAirBlockId});
    }
}

void QcCircuitState::forgetChunk(QcChunkPos chunk)
{
    waiting.remove(chunk);
    waitingOn.remove(chunk);
    const auto inChunk = [chunk](QcBlockPos pos) { return pos.chunk() == chunk; };
    changes.removeIf([&inChunk](const Change &change) { return inChunk(change.pos); });
    const QList<QcSignalSchedule::Entry> entries = schedule.entries();
    for (const QcSignalSchedule::Entry &entry : entries) {
        if (inChunk(entry.pos)) {
            schedule.remove(entry.pos);
        }
    }
    torchOffTicks.removeIf([&inChunk](const auto &it) { return inChunk(it.key()); });
    burntOutUntil.removeIf([&inChunk](const auto &it) { return inChunk(it.key()); });
    poweredOpeners.removeIf(inChunk);
    extending.removeIf([&inChunk](const auto &it) { return inChunk(it.key()); });
}

void QcCircuitState::clear()
{
    changes.clear();
    waiting.clear();
    waitingOn.clear();
    schedule.clear();
    torchOffTicks.clear();
    burntOutUntil.clear();
    poweredOpeners.clear();
    extending.clear();
}
