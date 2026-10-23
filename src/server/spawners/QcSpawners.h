#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/world/QcChunk.h"

#include <QHash>
#include <QList>
#include <QString>

#include <vector>

// The monster spawners of the loaded chunks (decision E-520): one entry per spawner block with the
// creature kind it brings forth and the ticks left until its next cycle, filed by chunk so that they
// are saved, unloaded and loaded with their chunk (QcBlockEntityRecord::Table::Spawners). Kept in step
// with the blocks by QcServerWorld: a spawner block set anywhere gets an entry with its block's kind
// (QcSpawnerDefinition::mob), one that goes loses it; a chunk that arrives gets entries for its
// spawner blocks, then restore() puts back what was saved. The rules that use the entries are
// QcSpawnerRules.
class QcSpawners
{
public:
    struct Entry
    {
        QcBlockPos pos;
        // Content name of the creature kind; empty: an empty cage that spawns nothing.
        QString mob;
        // Game ticks until the next cycle (counted only while a player is near).
        quint32 delay = firstDelay;
        // Its creatures are confined (QcCreature::confined; the showcase's dungeon): they walk, but
        // neither teleport, follow an owner by teleport, carry blocks nor change the world.
        bool confined = false;

        bool operator==(const Entry &) const = default;
    };

    // Ticks a new spawner waits before its first cycle (the reference's 20).
    static constexpr quint32 firstDelay = 20;
    // Longest delay kept from a save (damaged values are cut to it).
    static constexpr quint32 maxDelay = 1u << 20;

    explicit QcSpawners(const QcContentRegistry &content);

    bool isSpawner(QcBlockId block) const { return static_cast<std::size_t>(block) < m_spawner.size() && m_spawner[static_cast<std::size_t>(block)]; }

    // The block at `pos` changed from `before` to `after` (QcServerWorld::setBlock).
    void noteBlock(QcBlockPos pos, QcBlockId before, QcBlockId after);
    // A chunk entered the world: entries for its spawner blocks in the marked sections.
    void noteChunk(QcChunkPos pos, const QcChunk &chunk, quint32 sections);
    // Forgets the entries of a chunk (it left the world or was generated again).
    void forgetChunk(QcChunkPos pos);
    // The entries of a chunk in position order, to be saved; takeChunk() also forgets them.
    QList<Entry> savedOf(QcChunkPos pos) const;
    QList<Entry> takeChunk(QcChunkPos pos);
    // Puts back saved entries where the chunk still has a spawner block (others are dropped).
    void restore(const QList<Entry> &entries, const QcChunk &chunk);
    // Replaces the entry at a spawner block (/setblock with a kind, the world generator); false where
    // no spawner block is.
    bool set(const Entry &entry);

    const Entry *at(QcBlockPos pos) const;
    Entry *at(QcBlockPos pos);
    bool contains(QcBlockPos pos) const { return m_entries.contains(pos); }
    qsizetype size() const { return m_entries.size(); }
    // Every entry in position order (a stable order for the rules' random draws).
    QList<QcBlockPos> positions() const;
    QList<QcBlockPos> positionsIn(QcChunkPos pos) const { return m_byChunk.value(pos); }
    // Spawners whose kind changed, appeared or went since the last call (clients are told; a new
    // delay or `confined` alone is no change).
    QList<QcBlockPos> takeChanges();
    void clear();

private:
    void add(const Entry &entry);
    void remove(QcBlockPos pos);

    // Per block state: whether it is a spawner, and the kind its block starts with.
    std::vector<bool> m_spawner;
    std::vector<QString> m_defaultMob;
    QHash<QcBlockPos, Entry> m_entries;
    QHash<QcChunkPos, QList<QcBlockPos>> m_byChunk;
    QList<QcBlockPos> m_changes;
};
