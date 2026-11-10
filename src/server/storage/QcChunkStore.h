// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcChunkPos.h"
#include "common/core/QcResult.h"
#include "server/storage/QcGeneratedChunks.h"
#include "server/storage/QcRegionFile.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>

#include <array>
#include <optional>

/*! \brief The chunks of a world on disk (decision E-373): per kind (blocks, block entities,
    entities) one indexed region file per region (QcRegionFile), read chunk by chunk when a chunk is
    needed.

    What is handed over for writing waits in memory (\c {put}) until the next commit, which
    writes every changed region and the world's other files of a save as one: all files first
    under a staging name, then a journal naming them, then the renames, then the journal goes. A
    save that breaks off before the journal leaves the previous save whole; one that breaks off
    after it is finished when the world opens again (open()). Without a directory nothing is
    written and everything stays in memory. Not thread-safe: QcChunkIo uses it from its one
    thread. */
class QcChunkStore
{
public:
    /*! The kinds of data of a chunk, each in its own region files. */
    enum class Kind : quint8 {
        /*! The blocks (QcChunkCodec). */
        Blocks,
        /*! The block entities (QcBlockEntityCodec). */
        BlockEntities,
        /*! The entities (QcEntityCodec). */
        Entities
    };
    /*! The number of kinds. */
    static constexpr int kindCount = 3;

    /*! \brief What is kept of a chunk, without reading it. */
    struct Stored
    {
        /*! Bit (1 << kind) per kind with data. */
        quint8 kinds = 0;
        /*! The circuit marks of the chunk (QcChunkLinks), kept with its block entities. */
        quint16 marks = 0;

        /*! Returns whether the chunk has data of \a kind. */
        bool has(Kind kind) const { return (kinds & (1u << static_cast<quint8>(kind))) != 0; }
    };

    /*! \brief The data of a chunk as stored, per kind (a kind without data is missing), and its
        circuit marks. \qcfields */
    struct Blobs
    {
        std::array<std::optional<QByteArray>, kindCount> data;
        quint16 marks = 0;
    };

    /*! \brief A whole file of the world written with a commit (world.json, blockupdates.qce); no
        content removes it. */
    struct WholeFile
    {
        /*! Relative to the world directory, and the content (none: remove the file).
            \qcshared */
        QString name;
        std::optional<QByteArray> content;
    };

    /*! Name of the journal of a save in the world directory. */
    static constexpr QStringView journalName = u"save.journal";

    explicit QcChunkStore(QString directory);

    /*! Returns the world directory; empty when everything stays in memory. */
    const QString &directory() const { return m_directory; }
    /*! Returns the folder of the region files of \a kind ("regions", "blockentities",
        "entities"). */
    static QString folderOf(Kind kind);
    /*! Returns the file extension of the region files of \a kind ("qcr", "qcb", "qen"). */
    static QString extensionOf(Kind kind);

    /*! Finishes a save the journal names, removes staged files of a save that broke off and
        reads the index of every region file. Returns an error naming the file for a damaged index
        or a file of an older version (damaged()). */
    [[nodiscard]] QcStatus open();
    /*! \brief The kind and file (relative to the world directory) open() failed on;
        \c {journal}: the journal of a save that broke off cannot be finished (damaged, or a
        staged file is lost). \qcfields */
    struct Damaged
    {
        Kind kind = Kind::Blocks;
        QString file;
        bool journal = false;
    };
    /*! Returns the file open() failed on; std::nullopt when it did not. */
    const std::optional<Damaged> &damaged() const { return m_damaged; }

    /*! Returns every chunk with data, on disk or waiting to be written. */
    QHash<QcChunkPos, Stored> catalog() const;
    /*! Returns the data of \a chunk: what waits to be written, else what is on disk. A blob that
        fails its check is an error. */
    [[nodiscard]] QcResult<Blobs> read(QcChunkPos chunk);
    /*! Returns whether data of \a kind of \a chunk is on disk or waiting to be written. */
    bool has(Kind kind, QcChunkPos chunk) const;
    /*! Hands over \a blob, the data of \a kind of \a chunk, for the next commit (none: the chunk
        has no data of that kind any more). \a marks counts for block entities only. */
    void put(Kind kind, QcChunkPos chunk, std::optional<QByteArray> blob, quint16 marks = 0);
    /*! Returns the largest number of chunks of one kind with data waiting for the next commit
        (not the number of distinct chunks). */
    qsizetype pendingChunks() const;
    /*! Writes everything handed over and \a files as one save (see the class), with what the
        stores \a linked hold (the dimensions of the world, each in a folder below this store's;
        decision E-988): one journal here names the files of all of them, so a save that breaks
        off never leaves one dimension behind another. The linked stores keep a pointer to this
        store from now on, which must outlive them: after a commit that broke off they read
        nothing before it finished its journal. Returns an error on failure. Then nothing handed
        over is lost, the files included: it waits for the next commit. */
    [[nodiscard]] QcStatus commit(const QList<WholeFile> &files, const QList<QcChunkStore *> &linked = {});
    /*! Hands over \a files for the next commit. */
    void keepFiles(const QList<WholeFile> &files);
    /*! Returns the whole file \a name as the next commit writes it (waiting) or as it is on disk;
        none: no such file. */
    [[nodiscard]] QcResult<std::optional<QByteArray>> readFile(const QString &name);
    /*! Adds \a chunk to the index of generated chunks (QcGeneratedChunks), written with the next
        commit that follows. */
    void noteGenerated(QcChunkPos chunk) { m_generated.index.note(chunk); }
    /*! Returns the index of generated chunks: what open() read, with what was noted since. */
    const QcGeneratedChunks &generated() const { return m_generated.index; }
    /*! Returns the file (relative to the directory) the next commit writes the index to. */
    const QString &generatedFile() const { return m_generated.file; }
    /*! Returns the warnings of the last open() for the player (a damaged or newer index). */
    const QStringList &openWarnings() const { return m_generated.warnings; }
    /*! Test hook: renames of a commit after \a count fail (-1: none fail). */
    void failRenamesAfter(int count) { m_renamesLeft = count; }

private:
    struct Pending
    {
        std::optional<QByteArray> blob;
        quint16 marks = 0;
    };
    // A file the save replaces (staged at `name` + ".next") or removes.
    // A replacing step names the size and the checksum of the head of its staged file (header and
    // index of a region, all of a small file): finishing a journal tells a file already renamed from
    // one whose staged file was lost.
    struct Step
    {
        QString name;
        bool remove = false;
        qint64 size = 0;
        quint32 headCrc = 0;
    };

    // The renames a commit may still make before the test hook refuses one (-1: no limit).
    int renamesLeft() const { return m_renamesLeft; }
    QString absolute(const QString &name) const;
    // The chunks with data of `kind`, with their marks.
    [[nodiscard]] QHash<QcChunkPos, quint16> chunksOf(Kind kind) const;
    [[nodiscard]] QcStatus finishJournal();
    void removeStaged() const;
    [[nodiscard]] QcStatus readIndexes();
    void setGenerated(QcGeneratedChunks::Opened generated) { m_generated = std::move(generated); }
    void markGeneratedSaved() { m_generated.index.setUnsaved(false); }
    // A region staged: its step (none when nothing changes) and its index after the commit.
    struct StagedRegion
    {
        std::optional<Step> step;
        QcRegionFile::Index index;
    };
    // What a commit stages: the steps of its journal and the new index of every region it writes.
    struct Staging
    {
        QList<Step> steps;
        std::array<QHash<QcChunkPos, QcRegionFile::Index>, kindCount> written;
    };
    // The size and the checksum of the head of a file (Step::size, Step::headCrc).
    struct Head
    {
        qint64 size = 0;
        quint32 crc = 0;
    };

    // Stages the new file of `region` of `kind`.
    [[nodiscard]] QcResult<StagedRegion> stageRegion(Kind kind, QcChunkPos region);
    // Stages every region with changes and the whole files.
    [[nodiscard]] QcResult<Staging> stageAll();
    // Takes in what a commit wrote: the new indexes, nothing pending any more.
    void adoptStaging(const Staging &staging);
    // Gets the store ready for staging: the folder is there and the index of generated chunks goes
    // with the save.
    [[nodiscard]] QcStatus prepareCommit();
    [[nodiscard]] static QcResult<Head> headOf(const QString &path);
    [[nodiscard]] QcStatus writeJournal(const QList<Step> &steps) const;
    [[nodiscard]] QcStatus runSteps(const QList<Step> &steps);
    // After a commit broke off between its renames: finishes it from the journal and reads the
    // indexes again. The journal lies with the store that wrote it, which goes first: until then
    // the staged files here are still needed and the renames in this folder may be half done.
    [[nodiscard]] QcStatus reopenIfNeeded();

    QString m_directory;
    std::array<QHash<QcChunkPos, QcRegionFile::Index>, kindCount> m_indexes;
    std::array<QHash<QcChunkPos, Pending>, kindCount> m_pending;
    // Whole files of commits that failed, by name (the newest wins).
    QHash<QString, std::optional<QByteArray>> m_pendingFiles;
    std::optional<Damaged> m_damaged;
    // The index of generated chunks, its file and the warnings of reading it.
    QcGeneratedChunks::Opened m_generated;
    bool m_needsReopen = false;
    // The store whose journal names the files here (commit() with this store linked); null: this
    // store's own journal.
    QcChunkStore *m_journalStore = nullptr;
    int m_renamesLeft = -1;
};
