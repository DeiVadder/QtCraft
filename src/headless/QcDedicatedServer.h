#pragma once

#include "QcLoginMessages.h"
#include "QcResult.h"
#include "QcServerSettings.h"

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
    };

    QcDedicatedServer(std::shared_ptr<const QcContentRegistry> content, QList<QcPackFingerprint> packs, QObject *parent = nullptr);
    // Stops the server, which saves the world.
    ~QcDedicatedServer() override;

    QcStatus start(const Options &options);

private:
    // The configuration of the settings' world, made first when it does not exist.
    QcResult<QcServerConfig> worldConfig(const Options &options) const;
    void startConsole(bool stopAtEndOfInput);
    // Ends the event loop with `code`; the destructor saves.
    void stop(const QString &why, int code);

    std::shared_ptr<const QcContentRegistry> m_content;
    QList<QcPackFingerprint> m_packs;
    std::unique_ptr<QcLocalServerHost> m_host;
    QcSignalWatcher *m_signals = nullptr;
};
