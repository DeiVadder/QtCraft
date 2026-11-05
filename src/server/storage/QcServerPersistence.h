// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcResult.h"
#include "common/world/QcDamagedFile.h"
#include "server/storage/QcChunkStore.h"
#include "server/storage/QcWorldStorage.h"

#include <QHash>
#include <QList>
#include <QStringList>

#include <memory>
#include <optional>
#include <vector>

class QLockFile;

class QcBlockUpdateQueue;
class QcChunkIo;
class QcChunkLoading;
class QcServerBlocks;
class QcServerDimension;
struct QcServerClient;
class QcServerState;

/*! \brief Opening and saving the world of the server: world info, chunks with their block entities
    and entities (through QcChunkIo, read when they are needed), pending block updates and players.

    A save hands everything of the loaded chunks and world.json and blockupdates.qce to
    QcChunkIo, which writes them with the unloaded chunks waiting in memory as one (decision
    E-373); players are written at once. */
class QcServerPersistence
{
public:
    QcServerPersistence(QcServerState &state, QcServerBlocks &blocks, QcBlockUpdateQueue &updates, QcChunkLoading &loading);
    ~QcServerPersistence();
    Q_DISABLE_COPY_MOVE(QcServerPersistence)

    /*! Name of the lock file in the world directory, held while the world is open (decision
        E-093). */
    static constexpr QStringView lockFileName = u"session.lock";

    /*! Opens or creates the world directory given in the config (no-op without one); returns an
        error when it cannot. */
    [[nodiscard]] QcStatus open();
    /*! Writes everything and waits for it; returns the first error (see QcServer::save).
        No-op without storage. */
    [[nodiscard]] QcStatus save();
    /*! The same in the background (autosave, a player leaving): the tick does not wait for the
        disk; results come from takeNewFailure(). */
    void saveInBackground();
    /*! Takes the results of finished saves; returns the failure that started a streak of failed
        saves (one report per streak, decision E-378). */
    std::optional<QcStatus> takeNewFailure();
    /*! Returns whether a save in the background has not finished yet (at most one autosave runs
        at a time). */
    bool saveRunning() const { return m_running > 0; }
    /*! Returns whether the latest save failed (no early save until one succeeds). */
    bool savesFailing() const { return m_failing; }
    /*! Returns whether more unloaded chunks wait in memory than the limit unsavedChunks allows. */
    bool manyUnsavedChunks() const;
    /*! Waits until everything handed to the storage is written (tests, shutting down). */
    void waitForStorage();
    /*! Returns whether the world is kept in a directory. */
    bool hasStorage() const { return m_storage.has_value(); }
    /*! Returns the player \a name as the latest save handed his file over or as it is on disk
        (never saved: no player); without storage a player who was never saved. */
    [[nodiscard]] QcPlayerStorage::Result readPlayer(const QString &name) const;
    /*! Returns the problems found while opening the world (missing mods, unknown blocks); it
        still opened. */
    const QStringList &openWarnings() const { return m_openWarnings; }
    /*! Logs \a warnings and keeps them for the players. */
    void noteWarnings(const QStringList &warnings);
    /*! Returns whether open() failed because another server (in this or another process) has the
        world open. */
    bool worldInUse() const { return m_worldInUse; }
    /*! Returns whether open() failed because the world comes from an older development version. */
    bool worldTooOld() const { return m_worldTooOld; }
    /*! What a damaged file held (QcDamagedPart). */
    using DamagedPart = QcDamagedPart;
    /*! A damaged file of a world and what it held (QcDamagedFile). */
    using DamagedFile = QcDamagedFile;
    /*! Returns the damaged per-region file that made open() refuse the world. */
    const DamagedFile &damagedFile() const { return m_damagedFile; }

private:
    // Takes the world's lock file; fails when another server holds it.
    [[nodiscard]] QcStatus lockWorld();
    [[nodiscard]] QcStatus createInfo(const QcWorldStorage &storage);
    // Reads world.json; the warnings name differences of the content packs and limits out of range.
    [[nodiscard]] QcResult<QStringList> readInfo(const QcWorldStorage &storage);
    // Opens the chunk storage (a journal of a save that broke off is finished) and hands it to the world.
    [[nodiscard]] QcStatus openChunks(const QString &directory);
    [[nodiscard]] QcStatus readBlockUpdates(const QcWorldStorage &storage);
    // Reads villages.json of the current dimension; a damaged one is kept aside as
    // villages.json.damaged with a warning and the villages start empty, refilled from the plans as
    // their chunks load. Returns the change count of what was read; none: nothing was (the next save
    // writes the file).
    std::optional<quint64> restoreVillages(const QcWorldStorage &storage);
    // Opens what is stored of `dimension`, the current one, which is not the overworld: its chunks,
    // block updates, villages and dimension.json; a dimension opened for the first time gets the
    // one-time setup of its generator (decision E-988).
    [[nodiscard]] QcStatus openDimension(QcServerDimension &dimension);
    // Lets `dimension` generate the chunks its version map in `directory` names with their versions
    // (QcVersionMapFile, decision E-1012); an error for a damaged map or a version this game does not
    // generate.
    [[nodiscard]] QcStatus applyVersionMap(QcServerDimension &dimension, const QString &directory);
    // The current dimension's whole files of a save: blockupdates.qce, and villages.json when its
    // change count is not `villagesSaved` any more.
    [[nodiscard]] QcResult<QList<QcChunkStore::WholeFile>> dimensionFiles(std::optional<quint64> villagesSaved) const;
    // Hands everything of the dimensions other than the overworld to their storages.
    [[nodiscard]] QcStatus handDimensions();
    // Hands everything of a save to the storage, the players' files with the world's in one journal
    // (decision E-378); returns the id of the save and an error found while collecting.
    std::pair<quint64, QcStatus> submitSave();
    // The file of a player as a save writes it.
    QcChunkStore::WholeFile playerFile(const QcWorldStorage &storage, QcServerClient &client);
    // Notes the result of a finished save (see takeNewFailure).
    void noteResult(quint64 save, const QcStatus &status);
    // Hands the world's maps (maps/<id>.qcmap) to the map store, read when first needed.
    void openMaps();
    [[nodiscard]] QcResult<std::optional<QByteArray>> blockUpdateBytes() const;

    QcServerState &m_state;
    QcServerBlocks &m_blocks;
    QcBlockUpdateQueue &m_updates;
    QcChunkLoading &m_loading;
    std::optional<QcWorldStorage> m_storage;
    std::unique_ptr<QcChunkIo> m_io;
    // Saves submitted and not yet finished; failures not yet reported; whether the latest failed.
    int m_running = 0;
    std::optional<QcStatus> m_newFailure;
    bool m_failing = false;
    std::unique_ptr<QLockFile> m_lock;
    bool m_worldInUse = false;
    bool m_worldTooOld = false;
    // The change count of the villages (QcVillages::changes) last handed to a save; none: write
    // villages.json with the next save.
    std::optional<quint64> m_villagesSaved;
    // The storage of an open dimension other than the overworld (decision E-988).
    struct DimensionStore
    {
        QcServerDimension *dimension = nullptr;
        std::unique_ptr<QcChunkIo> io;
        // What m_villagesSaved is for the overworld.
        std::optional<quint64> villagesSaved;
    };
    std::vector<DimensionStore> &dimensionStores() { return m_dimensionStores; }
    const std::vector<DimensionStore> &dimensionStores() const { return m_dimensionStores; }
    std::vector<DimensionStore> m_dimensionStores;
    // The maps each running save writes (decision E-799).
    QHash<quint64, QList<quint32>> m_mapsBySave;
    DamagedFile m_damagedFile;
    QStringList m_openWarnings;
};
