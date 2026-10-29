// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcResult.h"
#include "common/protocol/QcLoginMessages.h"
#include "server/network/QcServerSettings.h"

#include <QObject>

#include <memory>

class QcContentRegistry;
class QcLocalServerHost;
class QcSignalWatcher;

// qtcraft-headless --server (decision E-255): a server without a local player that network
// clients join. It opens (or makes) the world of its settings below <data>/worlds, listens, and
// runs until /stop on the optional console, the time limit, or SIGTERM/SIGINT, which save the
// world on the way out. Without a console it reads no standard input (a service).
class QcDedicatedServer : public QObject
{
    Q_OBJECT

public:
    struct Options
    {
        QcServerSettings settings;
        QString dataDirectory;
        QString generatorOptions;
        bool console = false;
        bool stopAtEndOfInput = true;
        // Seconds until the server stops with exit code 2; 0 for none.
        int timeLimit = 0;
        // Seconds between health lines in the log; 0 for none (decision E-834).
        int healthSeconds = 0;
    };

    QcDedicatedServer(std::shared_ptr<const QcContentRegistry> content, QList<QcPackFingerprint> packs, QObject *parent = nullptr);
    // Stops the server, which saves the world.
    ~QcDedicatedServer() override;
    Q_DISABLE_COPY_MOVE(QcDedicatedServer)

    [[nodiscard]] QcStatus start(const Options &options);

private:
    // The configuration of the settings' world, made first when it does not exist.
    [[nodiscard]] QcResult<QcServerConfig> worldConfig(const Options &options) const;
    void startConsole(bool stopAtEndOfInput);
    // The world and mods in the log, the health line every `healthSeconds` (decision E-834).
    void startDiagnostics(const QcServerConfig &config, int healthSeconds);
    // The console's line "diagnostics": writes a bundle of the data directory.
    QStringList writeDiagnostics() const;
    // Ends the event loop with `code`; the destructor saves.
    void stop(const QString &why, int code);

    std::shared_ptr<const QcContentRegistry> m_content;
    QList<QcPackFingerprint> m_packs;
    std::unique_ptr<QcLocalServerHost> m_host;
    QcSignalWatcher *m_signals = nullptr;
    QString m_dataDirectory;
    // The world and the mods, for the log and the diagnostics bundle.
    QStringList m_worldInfo;
};
