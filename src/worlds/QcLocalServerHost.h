// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcMailbox.h"
#include "common/core/QcResult.h"
#include "common/core/QcTickSummary.h"
#include "common/diagnostics/QcHealthSample.h"
#include "common/protocol/QcLoginSettings.h"
#include "common/protocol/QcTransport.h"
#include "common/world/QcDamagedFile.h"
#include "network/QcNetworkConfig.h"
#include "worlds/QcWorldStart.h"

#include <QObject>

#include <memory>
#include <optional>

class QcContentRegistry;
class QcNetworkServer;
class QcServer;
struct QcServerConfig;
class QcWorldGenerator;
class QThread;

/*! \brief Runs a QcServer in its own thread inside the game process (single player) and hands out
    in-process client transports.

    Owned and used by the GUI/main thread. */
class QcLocalServerHost : public QObject
{
    Q_OBJECT

public:
    /*! Opens the world \a start describes, with the server's rules for a world of this process:
        the view distance one chunk ring beyond the player's, the default game mode only for an
        unsaved world. The server uses \a content and \a generator; \a parent is the QObject
        parent. */
    QcLocalServerHost(const QcWorldStart &start, std::shared_ptr<const QcContentRegistry> content,
                      std::unique_ptr<QcWorldGenerator> generator, QObject *parent = nullptr);
    /*! Opens a world with the server configuration \a config (headless tool and tests, which
        include server/core/QcServerConfig.h), \a content and \a generator; \a parent is the
        QObject parent. */
    QcLocalServerHost(QcServerConfig config, std::shared_ptr<const QcContentRegistry> content,
                      std::unique_ptr<QcWorldGenerator> generator, QObject *parent = nullptr);
    ~QcLocalServerHost() override;
    Q_DISABLE_COPY_MOVE(QcLocalServerHost)

    /*! Returns an empty string when the world opened; otherwise the reason, and the host serves
        no clients. */
    QString startError() const { return m_startError; }
    /*! Returns whether the world could not open because another server (window or process) has
        it open. */
    bool worldInUse() const { return m_worldInUse; }
    /*! Returns whether the world could not open because it comes from an older development
        version (decision E-356). */
    bool worldTooOld() const { return m_worldTooOld; }
    /*! Returns the damaged per-region file that kept the world from opening (decision E-366). */
    const QcDamagedFile &damagedFile() const { return m_damagedFile; }

    /*! Connects a new client; the returned transport belongs to the caller's thread. Clients of
        the host are local players (QcServer::ClientOrigin::Local: chat is not rate limited,
        debug data is sent). */
    std::unique_ptr<QcTransport> connectClient();
    /*! Saves the world now (blocks until done). */
    [[nodiscard]] QcStatus save();
    /*! Lets network clients join as well (decision E-251): a listener in the server thread, the
        local player stays in-process. Returns the port in use; listening again replaces the
        listener. \a config sets up the listener; \a login replaces the login settings of the
        server's configuration. Returns an error when no world is open or the port cannot be
        opened. */
    [[nodiscard]] QcResult<quint16> listen(const QcNetworkConfig &config, const std::optional<QcLoginSettings> &login = std::nullopt);
    /*! Closes the listener and disconnects the network players ("The host stopped sharing the
        world"); the local player plays on. */
    void stopListening();
    /*! Returns the port of the listener, 0 when not listening. */
    quint16 listeningPort() const { return m_listeningPort; }
    /*! Runs \a line as a line of the server console in the server thread (decision E-247) and
        returns the replies as English text (blocks until done). */
    QStringList runConsoleCommand(const QString &line);
    /*! Server tick durations, lateness and skipped ticks so far (benchmark); zero when no world
        is open. */
    QcTickSummary tickStats() const;
    /*! Clears the tick statistics that tickStats() reports, so a measurement starts afresh. */
    void resetTickTimes();
    /*! \brief Creatures and dropped item stacks on the server now (benchmark; blocks until the
        server thread counted them).

        All -1 when no world is open. */
    struct EntityCounts
    {
        /*! Creatures. */
        qsizetype mobs = -1;
        /*! Dropped item stacks. */
        qsizetype items = -1;
        /*! Containers, machines and saplings, and broken rules between them and the blocks
            (QcBlockEntityChecks, decision E-357). \qcshared */
        qsizetype blockEntities = -1;
        qsizetype blockEntityProblems = -1;
        /*! Broken rules of the entity register (QcEntityChecks). */
        qsizetype entityProblems = -1;
    };
    /*! Returns the entity counts, counted in the server thread (blocks until done); all -1 when
        no world is open. */
    EntityCounts entityCounts();
    /*! A tick running longer than this is named by health() (tick watchdog). */
    static constexpr qint64 stalledTickNs = qint64{10} * 1'000'000'000;
    /*! Returns the server's part of the health line (decision E-834): players, chunks, entities,
        network traffic and the tick times since the previous call (blocks until the server
        thread counted); empty when no world is open, and when a tick has run for longer than
        stalledTickNs: then it logs a warning with the phase that finished last instead of
        waiting for the server thread. */
    QcHealthSample health();
    /*! Stops the server (which saves) and joins its thread. Safe to call more than once; later
        calls report success. */
    [[nodiscard]] QcStatus shutdown();

signals:
    /*! The console asked to stop the server (/stop); the owner shuts the host down. */
    void stopRequested();

private:
    // Runs in the server thread.
    void acceptPendingClients(QcServer *server);

    QThread *m_thread = nullptr;
    QString m_startError;
    bool m_worldInUse = false;
    bool m_worldTooOld = false;
    QcDamagedFile m_damagedFile;
    QcMailbox<std::unique_ptr<QcTransport>> m_pendingClients;
    // Both live in the server thread and are created, used and destroyed only there (the network
    // listener before the server it refers to).
    std::unique_ptr<QcServer> m_server;
    std::unique_ptr<QcNetworkServer> m_network;
    quint16 m_listeningPort = 0;
};
