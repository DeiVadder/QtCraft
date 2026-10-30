// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcBlockPos.h"
#include "common/world/QcChunk.h"
#include "common/world/QcPlacedChunkLoader.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

#include <map>
#include <vector>

/*! \brief Chunk loader blocks (decision E-377): a block whose item carries the tag
    qtcraft:chunk_loaders keeps its own chunk loaded, and with it the group of circuits that chunk
    belongs to.

    The server knows every loader of the world, also those in chunks that are not loaded (they
    are kept in world.json), in the order they were placed; only the first
    QcServerLimits::chunkLoaders work. A player places at most chunkLoadersPerPlayer
    (QcServerInteraction); loaders set by commands or structures belong to no one. */
class QcChunkLoaders
{
public:
    /*! A placed loader: position and owner. */
    using Loader = QcPlacedChunkLoader;

    /*! The item tag that makes a block a chunk loader. */
    static constexpr auto tag = "qtcraft:chunk_loaders";

    explicit QcChunkLoaders(const QcContentRegistry &content);

    /*! Returns whether the state \a block is a chunk loader. */
    bool isLoader(QcBlockId block) const { return static_cast<std::size_t>(block) < m_loader.size() && m_loader[static_cast<std::size_t>(block)]; }
    /*! The block at \a pos changed from \a before to \a after in a loaded chunk. */
    void noteBlock(QcBlockPos pos, QcBlockId before, QcBlockId after);
    /*! \a chunk entered the world: its loaders are known now, those it no longer has are
        forgotten. */
    void noteChunk(const QcChunk &chunk);
    /*! Gives the loader at \a pos the owner \a owner (empty: no one). */
    void setOwner(QcBlockPos pos, const QString &owner);
    /*! Returns the number of loaders \a owner placed. */
    qsizetype countOf(const QString &owner) const { return m_perOwner.value(owner); }
    /*! Returns the number of loaders in the world. */
    qsizetype count() const { return m_byPos.size(); }
    /*! Returns the count of loaders found or gone (QcChunkLoading recomputes the needed chunks on a
        change). */
    quint64 changes() const { return m_changes; }
    /*! Returns all loaders in the order they were placed. */
    QList<Loader> loaders() const;
    /*! Sets \a loaders of a saved world (world.json); unplausible entries were left out when it was
        read. */
    void restore(const QList<Loader> &loaders);
    /*! Returns the chunks the first \a limit loaders keep loaded. */
    QList<QcChunkPos> chunks(qsizetype limit) const;

private:
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
