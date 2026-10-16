#include "QcBlockEntities.h"

#include <algorithm>
#include <utility>

bool QcBlockEntities::Machine::isCold() const
{
    return std::ranges::all_of(stacks, [](QcItemStack stack) { return stack.isEmpty(); }) && burnRemaining <= 0.0
           && burnSeconds <= 0.0 && progress <= 0.0;
}

bool QcBlockEntities::Machine::isIdle() const
{
    return isCold() && experience <= 0.0;
}

QcBlockEntities::QcBlockEntities(std::shared_ptr<const QcContentRegistry> content)
    : m_content(std::move(content))
{
    m_plain.resize(std::size_t(m_content->stateCount()));
    for (std::size_t id = 0; id < m_plain.size(); ++id) {
        const QcBlockDefinition &block = m_content->block(QcBlockId(id));
        m_plain[id] = block.containerSlots == 0 && !block.machine.isMachine() && !block.sapling.isSapling();
    }
}

int QcBlockEntities::containerSlots(QcBlockId block) const
{
    return m_content->isValidBlock(block) ? m_content->block(block).containerSlots : 0;
}

const QcMachineDefinition *QcBlockEntities::machineOf(QcBlockId block) const
{
    if (!m_content->isValidBlock(block)) {
        return nullptr;
    }
    const QcMachineDefinition &machine = m_content->block(block).machine;
    return machine.isMachine() ? &machine : nullptr;
}

bool QcBlockEntities::isSapling(QcBlockId block) const
{
    return m_content->isValidBlock(block) && m_content->block(block).sapling.isSapling();
}

QcSignalKind QcBlockEntities::kindOf(QcBlockId block) const
{
    return m_content->isValidBlock(block) ? m_content->block(block).signal.kind : QcSignalKind::None;
}

void QcBlockEntities::index(QcBlockPos pos, QList<QcBlockPos> ChunkIndex::*list)
{
    (m_byChunk[pos.chunk()].*list).append(pos);
}

void QcBlockEntities::unindex(QcBlockPos pos, QList<QcBlockPos> ChunkIndex::*list)
{
    const auto chunk = m_byChunk.find(pos.chunk());
    if (chunk == m_byChunk.end()) {
        return;
    }
    // Swapped with the last one: the order of a chunk's index does not matter.
    QList<QcBlockPos> &positions = (*chunk).*list;
    const qsizetype at = positions.indexOf(pos);
    if (at >= 0) {
        positions.swapItemsAt(at, positions.size() - 1);
        positions.removeLast();
    }
    if (chunk->isEmpty()) {
        m_byChunk.erase(chunk);
    }
}

void QcBlockEntities::removeContainer(QcBlockPos pos)
{
    if (m_containers.remove(pos)) {
        unindex(pos, &ChunkIndex::containers);
    }
}

void QcBlockEntities::removeMachine(QcBlockPos pos)
{
    if (m_machines.remove(pos)) {
        unindex(pos, &ChunkIndex::machines);
    }
}

void QcBlockEntities::removeMemory(QcBlockPos pos)
{
    if (m_memories.remove(pos)) {
        unindex(pos, &ChunkIndex::memories);
    }
}

void QcBlockEntities::placed(QcBlockPos pos, QcBlockId before, QcBlockId after)
{
    // Most block changes (terrain, liquids, circuits) hold no entry and keep the kind of component.
    if (isPlain(before) && isPlain(after) && kindOf(before) == kindOf(after)) {
        return;
    }

    // A machine first, then a container: the order in which their contents drop.
    placedMachine(pos, after);
    placedContainer(pos, after);
    // Another state of the same sapling (mods may give saplings states) keeps the timer.
    if (!m_content->isValidBlock(before) || !m_content->isValidBlock(after) || !m_content->isSameBlock(before, after)) {
        removeSapling(pos);
    }
    placedMemory(pos, before, after);
}

void QcBlockEntities::placedMachine(QcBlockPos pos, QcBlockId after)
{
    if (machineOf(after)) {
        ensure(pos, after);
        return;
    }

    const auto it = m_machines.constFind(pos);
    if (it != m_machines.constEnd()) {
        spill(pos, QList<QcItemStack>(it->stacks.begin(), it->stacks.end()), it->experience);
        removeMachine(pos);
    }
}

void QcBlockEntities::placedContainer(QcBlockPos pos, QcBlockId after)
{
    const int slotCount = containerSlots(after);
    Container *existing = container(pos);
    if (!existing) {
        if (slotCount > 0) {
            ensure(pos, after);
        }
        return;
    }

    if (slotCount == 0) {
        spill(pos, existing->stacks);
        removeContainer(pos);
    } else if (existing->stacks.size() != slotCount) {
        // A container of another size keeps what fits and spills the rest.
        QList<QcItemStack> cut = existing->stacks.size() > slotCount ? existing->stacks.mid(slotCount) : QList<QcItemStack>();
        existing->stacks.resize(slotCount);
        spill(pos, std::move(cut));
    }
}

bool QcBlockEntities::forgetUnfitting(Memory &memory, QcSignalKind kind)
{
    if (kind != QcSignalKind::Torch) {
        memory.torchOffTicks.clear();
        memory.burntOutUntil = 0;
    }
    if (kind != QcSignalKind::Opener) {
        memory.powered = false;
    }
    if (kind != QcSignalKind::Piston) {
        memory.extendingSince.reset();
    }
    return memory.isEmpty();
}

void QcBlockEntities::placedMemory(QcBlockPos pos, QcBlockId before, QcBlockId after)
{
    const QcSignalKind now = kindOf(after);
    if (kindOf(before) == now) {
        return;
    }

    if (now != QcSignalKind::Hopper) {
        if (Container *hopper = container(pos)) {
            hopper->readyAt = 0;
        }
    }

    const auto it = m_memories.find(pos);
    if (it != m_memories.end() && forgetUnfitting(*it, now)) {
        removeMemory(pos);
    }
}

void QcBlockEntities::spill(QcBlockPos pos, QList<QcItemStack> stacks, double experience)
{
    stacks.removeIf([](QcItemStack stack) { return stack.isEmpty(); });
    if (!stacks.isEmpty() || experience > 0.0) {
        m_spills.append({pos, std::move(stacks), experience});
    }
}

void QcBlockEntities::chunkArrived(const QcChunk &chunk, quint32 sections)
{
    reconcile(chunk);

    for (int index = 0; index < QcWorld::sectionCount; ++index) {
        if ((sections & (1u << index)) != 0) {
            ensureSection(chunk, index);
        }
    }
}

void QcBlockEntities::ensureSection(const QcChunk &chunk, int sectionIndex)
{
    // Palette entries first, so the cells are compared by index only.
    const QcChunkSection &section = chunk.section(sectionIndex);
    const QList<QcBlockId> &palette = section.palette();
    QList<bool> holds(palette.size());
    for (qsizetype i = 0; i < palette.size(); ++i) {
        holds[i] = containerSlots(palette[i]) > 0 || machineOf(palette[i]);
    }

    const QcBlockPos corner{chunk.pos().x * QcWorld::chunkSize, sectionIndex * QcWorld::sectionHeight, chunk.pos().z * QcWorld::chunkSize};
    constexpr qsizetype cells = qsizetype(QcWorld::chunkSize) * QcWorld::chunkSize * QcWorld::sectionHeight;
    const QList<quint16> &indices = section.indices();
    for (qsizetype cell = 0; cell < cells; ++cell) {
        const quint16 entry = section.isUniform() ? quint16(0) : indices[cell];
        if (!holds[entry]) {
            continue;
        }
        const int x = int(cell % QcWorld::chunkSize);
        const int z = int(cell / QcWorld::chunkSize % QcWorld::chunkSize);
        const int y = int(cell / (qsizetype(QcWorld::chunkSize) * QcWorld::chunkSize));
        ensure(corner.offset(x, y, z), palette[entry]);
    }
}

void QcBlockEntities::reconcile(const QcChunk &chunk)
{
    const auto kept = m_byChunk.constFind(chunk.pos());
    if (kept == m_byChunk.constEnd()) {
        return;
    }

    // Copied first: fitting an entry may take it (and the chunk's index) away.
    const ChunkIndex positions = *kept; // NOLINT(performance-unnecessary-copy-initialization): see above
    const auto blockAt = [&chunk](QcBlockPos pos) { return chunk.block(pos.localX(), pos.y, pos.localZ()); };
    for (const QcBlockPos &pos : positions.machines) {
        placedMachine(pos, blockAt(pos));
    }
    for (const QcBlockPos &pos : positions.containers) {
        placedContainer(pos, blockAt(pos));
        if (Container *hopper = container(pos); hopper && kindOf(blockAt(pos)) != QcSignalKind::Hopper) {
            hopper->readyAt = 0;
        }
    }
    for (const QcBlockPos &pos : positions.saplings) {
        if (!isSapling(blockAt(pos))) {
            removeSapling(pos);
        }
    }
    for (const QcBlockPos &pos : positions.memories) {
        const auto it = m_memories.find(pos);
        if (it != m_memories.end() && forgetUnfitting(*it, kindOf(blockAt(pos)))) {
            removeMemory(pos);
        }
    }
}

namespace {

// Moves the entry at `from` to `to` within one table; it stays where it is when `to` has one.
template<typename Entry>
bool moveEntry(QHash<QcBlockPos, Entry> &table, QcBlockPos from, QcBlockPos to)
{
    if (!table.contains(from) || table.contains(to)) {
        return false;
    }
    Entry entry = table.take(from);
    if constexpr (requires { entry.pos; }) {
        entry.pos = to;
    }
    table.insert(to, std::move(entry));
    return true;
}

} // namespace

void QcBlockEntities::relocate(QcBlockPos from, QcBlockPos to)
{
    if (moveEntry(m_containers, from, to)) {
        unindex(from, &ChunkIndex::containers);
        index(to, &ChunkIndex::containers);
    }
    if (moveEntry(m_machines, from, to)) {
        unindex(from, &ChunkIndex::machines);
        index(to, &ChunkIndex::machines);
    }
    if (moveEntry(m_memories, from, to)) {
        unindex(from, &ChunkIndex::memories);
        index(to, &ChunkIndex::memories);
    }
    if (moveEntry(m_saplingAt, from, to)) {
        m_saplings[m_saplingAt.value(to)].pos = to;
        unindex(from, &ChunkIndex::saplings);
        index(to, &ChunkIndex::saplings);
    }
    m_schedule.relocate(from, to);
}

void QcBlockEntities::forgetChunk(QcChunkPos chunk)
{
    const ChunkIndex positions = m_byChunk.take(chunk);
    for (const QcBlockPos &pos : positions.containers) {
        m_containers.remove(pos);
    }
    for (const QcBlockPos &pos : positions.machines) {
        m_machines.remove(pos);
    }
    for (const QcBlockPos &pos : positions.memories) {
        m_memories.remove(pos);
    }
    for (const QcBlockPos &pos : positions.saplings) {
        m_saplings.erase(m_saplingAt.take(pos));
    }
    m_schedule.forgetChunk(chunk);
}

QList<QcBlockEntities::Spill> QcBlockEntities::takeSpills()
{
    return std::exchange(m_spills, {});
}

QcBlockEntities::Container *QcBlockEntities::container(QcBlockPos pos)
{
    const auto it = m_containers.find(pos);
    return it == m_containers.end() ? nullptr : &*it;
}

const QcBlockEntities::Container *QcBlockEntities::container(QcBlockPos pos) const
{
    const auto it = m_containers.constFind(pos);
    return it == m_containers.constEnd() ? nullptr : &*it;
}

QcBlockEntities::Machine *QcBlockEntities::machine(QcBlockPos pos)
{
    const auto it = m_machines.find(pos);
    return it == m_machines.end() ? nullptr : &*it;
}

const QcBlockEntities::Machine *QcBlockEntities::machine(QcBlockPos pos) const
{
    const auto it = m_machines.constFind(pos);
    return it == m_machines.constEnd() ? nullptr : &*it;
}

bool QcBlockEntities::ensure(QcBlockPos pos, QcBlockId block)
{
    bool holds = false;
    if (const int slotCount = containerSlots(block); slotCount > 0) {
        if (!m_containers.contains(pos) && !isContainerTableFull()) {
            m_containers.insert(pos, {pos, QList<QcItemStack>(slotCount), 0});
            index(pos, &ChunkIndex::containers);
        }
        holds = m_containers.contains(pos);
    }

    if (machineOf(block)) {
        if (!m_machines.contains(pos) && !isMachineTableFull()) {
            Machine created;
            created.pos = pos;
            m_machines.insert(pos, created);
            index(pos, &ChunkIndex::machines);
        }
        holds = m_machines.contains(pos);
    }
    return holds;
}

bool QcBlockEntities::putContainer(const Container &container)
{
    if (container.stacks.isEmpty()) {
        return false;
    }
    if (!m_containers.contains(container.pos)) {
        index(container.pos, &ChunkIndex::containers);
    }
    m_containers.insert(container.pos, container);
    return true;
}

bool QcBlockEntities::putMachine(const Machine &machine)
{
    if (!m_machines.contains(machine.pos)) {
        index(machine.pos, &ChunkIndex::machines);
    }
    m_machines.insert(machine.pos, machine);
    return true;
}

bool QcBlockEntities::plantSapling(QcBlockPos pos, double remaining)
{
    removeSapling(pos);
    if (saplingCount() >= m_limits.saplings) {
        return false;
    }
    return putSapling({pos, remaining, m_nextSaplingOrder});
}

bool QcBlockEntities::putSapling(Sapling sapling)
{
    if (m_saplingAt.contains(sapling.pos) || m_saplings.contains(sapling.order)) {
        return false;
    }
    m_nextSaplingOrder = std::max(m_nextSaplingOrder, sapling.order + 1);
    m_saplingAt.insert(sapling.pos, sapling.order);
    m_saplings.emplace(sapling.order, sapling);
    index(sapling.pos, &ChunkIndex::saplings);
    return true;
}

const QcBlockEntities::Sapling *QcBlockEntities::sapling(QcBlockPos pos) const
{
    const auto at = m_saplingAt.constFind(pos);
    if (at == m_saplingAt.constEnd()) {
        return nullptr;
    }
    const auto it = m_saplings.find(*at);
    return it == m_saplings.end() ? nullptr : &it->second;
}

void QcBlockEntities::setSaplingRemaining(QcBlockPos pos, double remaining)
{
    if (const auto at = m_saplingAt.constFind(pos); at != m_saplingAt.constEnd()) {
        m_saplings[*at].remaining = remaining;
    }
}

void QcBlockEntities::removeSapling(QcBlockPos pos)
{
    const auto at = m_saplingAt.constFind(pos);
    if (at == m_saplingAt.constEnd()) {
        return;
    }
    m_saplings.erase(*at);
    m_saplingAt.erase(at);
    unindex(pos, &ChunkIndex::saplings);
}

QList<QcBlockEntities::Sapling> QcBlockEntities::saplingsInOrder() const
{
    QList<Sapling> saplings;
    saplings.reserve(qsizetype(m_saplings.size()));
    for (const auto &[order, sapling] : m_saplings) {
        saplings.append(sapling);
    }
    return saplings;
}

QList<quint64> QcBlockEntities::saplingOrders() const
{
    QList<quint64> orders;
    orders.reserve(qsizetype(m_saplings.size()));
    for (const auto &[order, sapling] : m_saplings) {
        orders.append(order);
    }
    return orders;
}

QcBlockEntities::Sapling *QcBlockEntities::saplingByOrder(quint64 order)
{
    const auto it = m_saplings.find(order);
    return it == m_saplings.end() ? nullptr : &it->second;
}

const QcBlockEntities::Memory *QcBlockEntities::memory(QcBlockPos pos) const
{
    const auto it = m_memories.constFind(pos);
    return it == m_memories.constEnd() ? nullptr : &*it;
}

QcBlockEntities::Memory &QcBlockEntities::memoryAt(QcBlockPos pos)
{
    auto it = m_memories.find(pos);
    if (it == m_memories.end()) {
        it = m_memories.insert(pos, {});
        index(pos, &ChunkIndex::memories);
    }
    return *it;
}

void QcBlockEntities::prune(quint64 now, quint64 torchWindow, quint64 extensionTicks)
{
    QList<QcBlockPos> empty;
    for (auto it = m_memories.begin(); it != m_memories.end(); ++it) {
        Memory &memory = *it;
        if (!memory.torchOffTicks.isEmpty() && memory.torchOffTicks.constLast() + torchWindow <= now) {
            memory.torchOffTicks.clear();
        }
        if (memory.burntOutUntil != 0 && memory.burntOutUntil <= now) {
            memory.burntOutUntil = 0;
        }
        if (memory.extendingSince && *memory.extendingSince + extensionTicks <= now) {
            memory.extendingSince.reset();
        }
        if (memory.isEmpty()) {
            empty.append(it.key());
        }
    }
    for (const QcBlockPos &pos : std::as_const(empty)) {
        removeMemory(pos);
    }
    for (Container &container : m_containers) {
        if (container.readyAt != 0 && container.readyAt <= now) {
            container.readyAt = 0;
        }
    }
}

const QcBlockEntities::ChunkIndex *QcBlockEntities::chunkIndex(QcChunkPos pos) const
{
    const auto it = m_byChunk.constFind(pos);
    return it == m_byChunk.constEnd() ? nullptr : &*it;
}

void QcBlockEntities::chunkLeft(QcChunkPos pos)
{
    const ChunkIndex *index = chunkIndex(pos);
    if (!index) {
        return;
    }

    const ChunkIndex positions = *index;
    for (const QcBlockPos &entry : positions.containers) {
        const Container &container = m_containers[entry];
        if (container.readyAt == 0 && std::ranges::all_of(container.stacks, [](QcItemStack stack) { return stack.isEmpty(); })) {
            removeContainer(entry);
        }
    }
    for (const QcBlockPos &entry : positions.machines) {
        if (m_machines[entry].isIdle()) {
            removeMachine(entry);
        }
    }
}

std::optional<QcBlockEntities::Chunk> QcBlockEntities::chunk(QcChunkPos pos) const
{
    const auto it = m_byChunk.constFind(pos);
    if (it == m_byChunk.constEnd()) {
        return std::nullopt;
    }

    Chunk chunk;
    for (const QcBlockPos &entry : it->containers) {
        chunk.containers.insert(entry, m_containers.value(entry));
    }
    for (const QcBlockPos &entry : it->machines) {
        chunk.machines.insert(entry, m_machines.value(entry));
    }
    for (const QcBlockPos &entry : it->saplings) {
        if (const Sapling *found = sapling(entry)) {
            chunk.saplings.insert(entry, *found);
        }
    }
    for (const QcBlockPos &entry : it->memories) {
        chunk.memories.insert(entry, m_memories.value(entry));
    }
    return chunk;
}

void QcBlockEntities::clear()
{
    m_containers.clear();
    m_machines.clear();
    m_memories.clear();
    m_saplings.clear();
    m_saplingAt.clear();
    m_byChunk.clear();
    m_schedule.clear();
    m_spills.clear();
}
