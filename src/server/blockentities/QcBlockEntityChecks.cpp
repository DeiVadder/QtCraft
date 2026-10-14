#include "QcBlockEntityChecks.h"

#include "QcServerWorld.h"

#include <QSet>

using namespace Qt::StringLiterals;

namespace {

QString at(QcBlockPos pos)
{
    return u"%1 %2 %3"_s.arg(pos.x).arg(pos.y).arg(pos.z);
}

// Entries of a loaded chunk against its blocks.
void checkEntries(const QcBlockEntities &entities, const QcChunk &chunk, const QcBlockEntities::Chunk &kept, QStringList &problems)
{
    const QcContentRegistry &content = entities.content();
    const auto blockAt = [&chunk](QcBlockPos pos) { return chunk.block(pos.localX(), pos.y, pos.localZ()); };
    const auto kindAt = [&](QcBlockPos pos) { return content.block(blockAt(pos)).signal.kind; };
    for (const QcBlockEntities::Container &container : kept.containers) {
        if (entities.containerSlots(blockAt(container.pos)) != container.stacks.size()) {
            problems.append(u"container at %1 does not fit its block"_s.arg(at(container.pos)));
        }
        if (container.readyAt != 0 && kindAt(container.pos) != QcSignalKind::Hopper) {
            problems.append(u"cooldown at %1 without a hopper"_s.arg(at(container.pos)));
        }
    }
    for (const QcBlockEntities::Machine &machine : kept.machines) {
        if (!entities.machineOf(blockAt(machine.pos))) {
            problems.append(u"machine at %1 without a machine block"_s.arg(at(machine.pos)));
        }
    }
    for (const QcBlockEntities::Sapling &sapling : kept.saplings) {
        if (!entities.isSapling(blockAt(sapling.pos))) {
            problems.append(u"sapling timer at %1 without a sapling"_s.arg(at(sapling.pos)));
        }
    }
    for (auto it = kept.memories.constBegin(); it != kept.memories.constEnd(); ++it) {
        const QcSignalKind kind = kindAt(it.key());
        const bool fits = (it->burntOutUntil == 0 && it->torchOffTicks.isEmpty()) || kind == QcSignalKind::Torch;
        if (!fits || (it->powered && kind != QcSignalKind::Opener) || (it->extendingSince && kind != QcSignalKind::Piston)) {
            problems.append(u"memory at %1 does not fit its block"_s.arg(at(it.key())));
        }
    }
}

// Blocks of one section of a loaded chunk that should have a container or machine.
void checkSection(const QcBlockEntities &entities, const QcChunk &chunk, int index, QStringList &problems)
{
    constexpr int cells = QcWorld::chunkSize * QcWorld::chunkSize * QcWorld::sectionHeight;
    for (int cell = 0; cell < cells; ++cell) {
        const int x = cell % QcWorld::chunkSize;
        const int z = cell / QcWorld::chunkSize % QcWorld::chunkSize;
        const int y = index * QcWorld::sectionHeight + cell / (QcWorld::chunkSize * QcWorld::chunkSize);
        const QcBlockId block = chunk.block(x, y, z);
        const QcBlockPos pos{chunk.pos().x * QcWorld::chunkSize + x, y, chunk.pos().z * QcWorld::chunkSize + z};
        if (entities.containerSlots(block) > 0 && !entities.container(pos) && !entities.isContainerTableFull()) {
            problems.append(u"container block at %1 without a container"_s.arg(at(pos)));
        }
        if (entities.machineOf(block) && !entities.machine(pos) && !entities.isMachineTableFull()) {
            problems.append(u"machine block at %1 without a machine"_s.arg(at(pos)));
        }
    }
}

// Blocks of a loaded chunk that should have a container or machine.
void checkBlocks(const QcServerWorld &world, const QcChunk &chunk, QStringList &problems)
{
    const quint32 sections = world.marks().sectionsOf(chunk.pos(), QcBlockTrait::BlockEntity);
    for (int index = 0; index < QcWorld::sectionCount; ++index) {
        if ((sections & (1u << index)) != 0) {
            checkSection(world.blockEntities(), chunk, index, problems);
        }
    }
}

// The counts and indexes of the register against its entries.
void checkBookkeeping(const QcBlockEntities &entities, QStringList &problems)
{
    qsizetype containers = 0;
    qsizetype machines = 0;
    qsizetype saplings = 0;
    const QList<QcChunkPos> chunks = entities.chunks();
    for (const QcChunkPos &pos : chunks) {
        const std::optional<QcBlockEntities::Chunk> chunk = entities.chunk(pos);
        if (!chunk || chunk->isEmpty()) {
            problems.append(u"empty record of chunk %1 %2"_s.arg(pos.x).arg(pos.z));
            continue;
        }
        // Each position once per kind in the chunk's index.
        const QcBlockEntities::ChunkIndex &index = *entities.chunkIndex(pos);
        const auto unique = [](const QList<QcBlockPos> &list) { return QSet<QcBlockPos>(list.begin(), list.end()).size() == list.size(); };
        if (!unique(index.containers) || !unique(index.machines) || !unique(index.saplings) || !unique(index.memories)) {
            problems.append(u"position filed twice in chunk %1 %2"_s.arg(pos.x).arg(pos.z));
        }
        const auto outside = [&pos](QcBlockPos entry) { return entry.chunk() != pos; };
        if (std::ranges::any_of(chunk->containers.keys(), outside) || std::ranges::any_of(chunk->machines.keys(), outside)
            || std::ranges::any_of(chunk->saplings.keys(), outside) || std::ranges::any_of(chunk->memories.keys(), outside)) {
            problems.append(u"entry outside its chunk %1 %2"_s.arg(pos.x).arg(pos.z));
        }
        containers += chunk->containers.size();
        machines += chunk->machines.size();
        saplings += chunk->saplings.size();
    }
    if (containers != entities.containerCount() || machines != entities.machineCount() || saplings != entities.saplingCount()
        || saplings != entities.saplingsInOrder().size()) {
        problems.append(u"counts do not match the entries"_s);
    }

    qsizetype scheduled = 0;
    const QList<QcChunkPos> scheduleChunks = entities.schedule().chunks();
    for (const QcChunkPos &pos : scheduleChunks) {
        const QList<QcSignalSchedule::Entry> entries = entities.schedule().entriesIn(pos);
        scheduled += entries.size();
        if (std::ranges::any_of(entries, [&pos](const QcSignalSchedule::Entry &entry) { return entry.pos.chunk() != pos; })) {
            problems.append(u"scheduled tick filed under the wrong chunk %1 %2"_s.arg(pos.x).arg(pos.z));
        }
    }
    if (scheduled != entities.schedule().size()) {
        problems.append(u"scheduled ticks missing from their chunks"_s);
    }
}

} // namespace

namespace QcBlockEntityChecks {

QStringList problems(const QcServerWorld &world)
{
    QStringList found;
    const QcBlockEntities &entities = world.blockEntities();
    checkBookkeeping(entities, found);
    const QList<QcChunkPos> loaded = world.loadedChunks();
    for (const QcChunkPos &pos : loaded) {
        const std::shared_ptr<const QcChunk> chunk = world.chunk(pos);
        if (const std::optional<QcBlockEntities::Chunk> kept = entities.chunk(pos)) {
            checkEntries(entities, *chunk, *kept, found);
        }
        checkBlocks(world, *chunk, found);
    }
    return found;
}

} // namespace QcBlockEntityChecks
