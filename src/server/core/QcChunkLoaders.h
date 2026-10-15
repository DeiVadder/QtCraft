#pragma once

#include "QcBlockPos.h"
#include "QcChunk.h"
#include "QcContentRegistry.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

#include <map>
#include <vector>

// Chunk loader blocks (decision E-377): a block whose item carries the tag qtcraft:chunk_loaders
// keeps its own chunk loaded, and with it the group of circuits that chunk belongs to. The server
// knows every loader of the world, also those in chunks that are not loaded (they are kept in
// world.json), in the order they were placed; only the first QcServerLimits::chunkLoaders work. A
// player places at most chunkLoadersPerPlayer (QcServerInteraction); loaders set by commands or
// structures belong to no one.
class QcChunkLoaders
{
public:
    struct Loader
    {
        QcBlockPos pos;
        // The player who placed it; empty for commands and structures.
        QString owner;

        bool operator==(const Loader &) const = default;
    };

    static constexpr auto tag = "qtcraft:chunk_loaders";

    explicit QcChunkLoaders(const QcContentRegistry &content);

    bool isLoader(QcBlockId block) const { return std::size_t(block) < m_loader.size() && m_loader[std::size_t(block)]; }
    // A block was set in a loaded chunk.
    void noteBlock(QcBlockPos pos, QcBlockId before, QcBlockId after);
    // A chunk entered the world: its loaders are known now, those it no longer has are forgotten.
    void noteChunk(const QcChunk &chunk);
    void setOwner(QcBlockPos pos, const QString &owner);
    qsizetype countOf(const QString &owner) const { return m_perOwner.value(owner); }
    qsizetype count() const { return m_byPos.size(); }
    // Counts every loader found or gone (QcChunkLoading recomputes the needed chunks on a change).
    quint64 changes() const { return m_changes; }
    // All loaders in the order they were placed.
    QList<Loader> loaders() const;
    // The loaders of a saved world (world.json); unplausible entries were left out when it was read.
    void restore(const QList<Loader> &loaders);
    // The chunks the first `limit` loaders keep loaded.
    QList<QcChunkPos> chunks(qsizetype limit) const;

private:
    // Adds the loaders of a section whose lowest corner is `corner`.
    void findIn(const QcChunkSection &section, QcBlockPos corner, QSet<QcBlockPos> &found) const;

    void add(QcBlockPos pos, const QString &owner);
    void remove(QcBlockPos pos);

    struct Entry
    {
        quint64 order = 0;
        QString owner;
    };

    std::vector<bool> m_loader;
    // Every loader by position and by the order it was placed in (lookups do not walk the list:
    // a /fill of loaders or a mod may place many).
    QHash<QcBlockPos, Entry> m_byPos;
    std::map<quint64, QcBlockPos> m_byOrder;
    QHash<QString, qsizetype> m_perOwner;
    quint64 m_nextOrder = 0;
    quint64 m_changes = 0;
};
