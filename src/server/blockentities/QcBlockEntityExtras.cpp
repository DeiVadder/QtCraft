#include "server/blockentities/QcBlockEntityExtras.h"

#include "server/blockentities/QcBlockEntities.h"
#include "server/blocks/QcFireQueue.h"
#include "server/machines/QcCampfires.h"
#include "server/spawners/QcSpawners.h"

void QcBlockEntityExtras::keepParts(QcChunkPos pos, QcBlockEntityKept parts)
{
    if (parts.isEmpty()) {
        m_parts.remove(pos);
    } else {
        m_parts[pos] = std::move(parts);
    }
}

void QcBlockEntityExtras::dropOrphanTails(QcBlockPos pos, const QcBlockEntities &entities, const QcFireQueue &fires, const QcSpawners &spawners,
                                          const QcCampfires &campfires)
{
    const auto parts = m_parts.find(pos.chunk());
    if (parts == m_parts.end() || parts->entries.isEmpty()) {
        return;
    }
    const auto tails = parts->entries.find(pos);
    if (tails == parts->entries.end()) {
        return;
    }

    // Scheduled ticks stay with a changed block (QcBlockEntities::placed); the backstop for them
    // and for entries that end without a block change is QcBlockEntityRecord::pruneKept.
    using Table = QcBlockEntityRecord::Table;
    tails->removeIf([&](const QcEntityComponent &tail) {
        switch (Table(tail.id)) {
        case Table::Containers:
            return entities.container(pos) == nullptr;
        case Table::Machines:
            return entities.machine(pos) == nullptr;
        case Table::Saplings:
            return entities.sapling(pos) == nullptr;
        case Table::Memories:
            return entities.memory(pos) == nullptr;
        case Table::Fires:
            return !fires.contains(pos);
        case Table::Spawners:
            return !spawners.contains(pos);
        case Table::Campfires:
            return campfires.at(pos) == nullptr;
        default:
            return false;
        }
    });
    if (tails->isEmpty()) {
        parts->entries.erase(tails);
    }
    if (parts->isEmpty()) {
        m_parts.erase(parts);
    }
}

void QcBlockEntityExtras::forgetChunk(QcChunkPos pos)
{
    m_furnished.remove(pos);
    m_parts.remove(pos);
}
