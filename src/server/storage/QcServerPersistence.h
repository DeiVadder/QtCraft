#pragma once

#include "QcChunkStore.h"
#include "QcResult.h"
#include "QcWorldStorage.h"

#include <QStringList>

#include <memory>
#include <optional>

class QLockFile;

class QcBlockUpdateQueue;
class QcChunkIo;
class QcChunkLoading;
class QcServerBlocks;
struct QcServerClient;
class QcServerState;

// Opening and saving the world of the server: world info, chunks with their block entities and
// entities (through QcChunkIo, read when they are needed), pending block updates and players. A
// save hands everything of the loaded chunks and world.json and blockupdates.qce to QcChunkIo, which
// writes them with the unloaded chunks waiting in memory as one (decision E-373); players are written
// at once.
class QcServerPersistence
{
public:
    QcServerPersistence(QcServerState &state, QcServerBlocks &blocks, QcBlockUpdateQueue &updates, QcChunkLoading &loading);
    ~QcServerPersistence();

    // Name of the lock file in the world directory, held while the world is open (decision E-093).
    static constexpr QStringView lockFileName = u"session.lock";

    // Opens or creates the world directory given in the config (no-op without one).
    [[nodiscard]] QcStatus open();
    // Writes everything and waits for it; the first error is returned (see QcServer::save). No-op
    // without storage.
    [[nodiscard]] QcStatus save();
    // The same in the background (autosave, a player leaving): the tick does not wait for the disk;
    // results come from takeNewFailures().
    void saveInBackground();
    // Takes the results of finished saves; returns the failure that started a streak of failed saves
    // (one report per streak, decision E-378).
    std::optional<QcStatus> takeNewFailure();
    // A save in the background has not finished yet (at most one autosave runs at a time).
    bool saveRunning() const { return m_running > 0; }
    // The latest save failed (no early save until one succeeds).
    bool savesFailing() const { return m_failing; }
    // More unloaded chunks wait in memory than the limit unsavedChunks allows.
    bool manyUnsavedChunks() const;
    // Waits until everything handed to the storage is written (tests, shutting down).
    void waitForStorage();
    bool hasStorage() const { return m_storage.has_value(); }
    [[nodiscard]] QcResult<std::optional<QcPlayerData>> readPlayer(const QString &name, QStringList &warnings) const;
    // Problems found while opening the world (missing mods, unknown blocks); it still opened.
    const QStringList &openWarnings() const { return m_openWarnings; }
    // Logs the warnings and keeps them for the players.
    void noteWarnings(const QStringList &warnings);
    // Whether open() failed because another server (in this or another process) has the world open.
    bool worldInUse() const { return m_worldInUse; }
    // Whether open() failed because the world comes from an older development version.
    bool worldTooOld() const { return m_worldTooOld; }
    // What a damaged per-region file of the world held (decision E-366): open() refuses the world
    // and names the file; moved out of the world folder, the world opens without what it held.
    // Journal: the last save broke off and cannot be finished; without its journal and staged files
    // the world opens as saved before (decision E-378).
    enum class DamagedPart { None, Blocks, Entities, BlockEntities, Journal };
    struct DamagedFile
    {
        DamagedPart part = DamagedPart::None;
        // Relative to the world directory.
        QString file;
    };
    const DamagedFile &damagedFile() const { return m_damagedFile; }

private:
    // Takes the world's lock file; fails when another server holds it.
    [[nodiscard]] QcStatus lockWorld();
    [[nodiscard]] QcStatus createInfo(const QcWorldStorage &storage);
    // Reads world.json; differences of the content packs are added to `warnings`.
    [[nodiscard]] QcStatus readInfo(const QcWorldStorage &storage, QStringList &warnings);
    // Opens the chunk storage (a journal of a save that broke off is finished) and hands it to the world.
    [[nodiscard]] QcStatus openChunks(const QString &directory);
    [[nodiscard]] QcStatus readBlockUpdates(const QcWorldStorage &storage);
    // Hands everything of a save to the storage, the players' files with the world's in one journal
    // (decision E-378); returns the id of the save and an error found while collecting.
    std::pair<quint64, QcStatus> submitSave();
    // The file of a player as a save writes it.
    QcChunkStore::WholeFile playerFile(const QcWorldStorage &storage, const QcServerClient &client) const;
    // Notes the result of a finished save (see takeNewFailure).
    void noteResult(const QcStatus &status);
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
    DamagedFile m_damagedFile;
    QStringList m_openWarnings;
};
