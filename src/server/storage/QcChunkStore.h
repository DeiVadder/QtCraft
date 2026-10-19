#pragma once

#include "common/core/QcChunkPos.h"
#include "common/core/QcResult.h"
#include "server/storage/QcRegionFile.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>

#include <array>
#include <optional>

// The chunks of a world on disk (decision E-373): per kind (blocks, block entities, entities) one
// indexed region file per region (QcRegionFile), read chunk by chunk when a chunk is needed. What is
// handed over for writing waits in memory (`put`) until the next commit, which writes every changed
// region and the world's other files of a save as one: all files first under a staging name, then a
// journal naming them, then the renames, then the journal goes. A save that breaks off before the
// journal leaves the previous save whole; one that breaks off after it is finished when the world
// opens again (open()). Without a directory nothing is written and everything stays in memory.
// Not thread-safe: QcChunkIo uses it from its one thread.
class QcChunkStore
{
public:
    enum class Kind : quint8 { Blocks, BlockEntities, Entities };
    static constexpr int kindCount = 3;

    // What is kept of a chunk, without reading it.
    struct Stored
    {
        // Bit (1 << kind) per kind with data.
        quint8 kinds = 0;
        // The circuit marks of the chunk (QcChunkLinks), kept with its block entities.
        quint16 marks = 0;

        bool has(Kind kind) const { return (kinds & (1u << quint8(kind))) != 0; }
    };

    // The data of a chunk as stored; a kind without data is missing.
    struct Blobs
    {
        std::array<std::optional<QByteArray>, kindCount> data;
        quint16 marks = 0;
    };

    // A whole file of the world written with a commit (world.json, blockupdates.qce); no content
    // removes it.
    struct WholeFile
    {
        // Relative to the world directory.
        QString name;
        std::optional<QByteArray> content;
    };

    // Name of the journal of a save in the world directory.
    static constexpr QStringView journalName = u"save.journal";

    explicit QcChunkStore(QString directory);

    const QString &directory() const { return m_directory; }
    static QString folderOf(Kind kind);
    static QString extensionOf(Kind kind);

    // Finishes a save the journal names, removes staged files of a save that broke off and reads the
    // index of every region file. A damaged index or a file of an older version is an error naming
    // the file (damagedFile()).
    [[nodiscard]] QcStatus open();
    // The kind and file (relative to the world directory) open() failed on; `journal`: the journal
    // of a save that broke off cannot be finished (damaged, or a staged file is lost).
    struct Damaged
    {
        Kind kind = Kind::Blocks;
        QString file;
        bool journal = false;
    };
    const std::optional<Damaged> &damaged() const { return m_damaged; }

    // Every chunk with data, on disk or waiting to be written.
    QHash<QcChunkPos, Stored> catalog() const;
    // The data of a chunk: what waits to be written, else what is on disk. A blob that fails its
    // check is an error.
    [[nodiscard]] QcResult<Blobs> read(QcChunkPos chunk);
    // Whether a kind of data of a chunk is on disk or waiting to be written.
    bool has(Kind kind, QcChunkPos chunk) const;
    // Hands over the data of a kind of a chunk for the next commit (none: the chunk has no data of
    // that kind any more). `marks` counts for block entities only.
    void put(Kind kind, QcChunkPos chunk, std::optional<QByteArray> blob, quint16 marks = 0);
    // Chunks with data waiting for the next commit.
    qsizetype pendingChunks() const;
    // Writes everything handed over and `files` as one save (see the class). On failure nothing
    // handed over is lost, the files included: it waits for the next commit.
    [[nodiscard]] QcStatus commit(const QList<WholeFile> &files);
    // A whole file as the next commit writes it (waiting) or as it is on disk; none: no such file.
    [[nodiscard]] QcResult<std::optional<QByteArray>> readFile(const QString &name);
    // Test hook: renames of a commit after this many fail (-1: none fail).
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

    QString absolute(const QString &name) const;
    // The chunks with data of `kind`, with their marks.
    [[nodiscard]] QHash<QcChunkPos, quint16> chunksOf(Kind kind) const;
    [[nodiscard]] QcStatus finishJournal();
    void removeStaged() const;
    [[nodiscard]] QcStatus readIndexes();
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
    [[nodiscard]] static QcResult<Head> headOf(const QString &path);
    [[nodiscard]] QcStatus writeJournal(const QList<Step> &steps) const;
    [[nodiscard]] QcStatus runSteps(const QList<Step> &steps);
    // After a commit broke off between its renames: finishes it from the journal and reads the
    // indexes again.
    [[nodiscard]] QcStatus reopenIfNeeded();

    QString m_directory;
    std::array<QHash<QcChunkPos, QcRegionFile::Index>, kindCount> m_indexes;
    std::array<QHash<QcChunkPos, Pending>, kindCount> m_pending;
    // Whole files of commits that failed, by name (the newest wins).
    QHash<QString, std::optional<QByteArray>> m_pendingFiles;
    std::optional<Damaged> m_damaged;
    bool m_needsReopen = false;
    int m_renamesLeft = -1;
};
