#pragma once

#include "QcResult.h"
#include "QcWorldStorage.h"

#include <QStringList>

#include <memory>
#include <optional>

class QLockFile;

class QcBlockUpdateQueue;
class QcServerBlocks;
struct QcServerClient;
struct QcServerState;

// Opening and saving the world of the server: world info, chunks, entities, mobs, machines, chests,
// pending block updates and players.
class QcServerPersistence
{
public:
    QcServerPersistence(QcServerState &state, QcServerBlocks &blocks, QcBlockUpdateQueue &updates);
    ~QcServerPersistence();

    // Name of the lock file in the world directory, held while the world is open (decision E-093).
    static constexpr QStringView lockFileName = u"session.lock";

    // Opens or creates the world directory given in the config (no-op without one).
    QcStatus open();
    // Writes everything; the first error is returned (see QcServer::save). No-op without storage.
    QcStatus save();
    QcStatus savePlayer(const QcServerClient &client);
    bool hasStorage() const { return m_storage.has_value(); }
    QcResult<std::optional<QcPlayerData>> readPlayer(const QString &name, QStringList &warnings) const;
    // Problems found while opening the world (missing mods, unknown blocks); it still opened.
    const QStringList &openWarnings() const { return m_openWarnings; }
    // Whether open() failed because another server (in this or another process) has the world open.
    bool worldInUse() const { return m_worldInUse; }

private:
    // Takes the world's lock file; fails when another server holds it.
    QcStatus lockWorld();
    QcStatus createInfo(const QcWorldStorage &storage);
    // Reads world.json; differences of the content packs are added to `warnings`.
    QcStatus readInfo(const QcWorldStorage &storage, QStringList &warnings);
    // Returns the number of stored chunks.
    QcResult<qsizetype> readChunks(const QcWorldStorage &storage, QStringList &warnings);
    QcStatus readEntities(const QcWorldStorage &storage);
    QcStatus readMobs(const QcWorldStorage &storage);
    QcStatus readMachines(const QcWorldStorage &storage);
    QcStatus readContainers(const QcWorldStorage &storage);
    QcStatus readBlockUpdates(const QcWorldStorage &storage);
    // Logs the warnings and keeps them for the players.
    void noteWarnings(const QStringList &warnings);
    QcStatus saveEntities(const QcWorldStorage &storage) const;
    QcStatus saveBlockUpdates(const QcWorldStorage &storage) const;

    QcServerState &m_state;
    QcServerBlocks &m_blocks;
    QcBlockUpdateQueue &m_updates;
    std::optional<QcWorldStorage> m_storage;
    std::unique_ptr<QLockFile> m_lock;
    bool m_worldInUse = false;
    QStringList m_openWarnings;
};
