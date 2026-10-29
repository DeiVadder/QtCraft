// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcResult.h"
#include "server/core/QcServerConfig.h"
#include "worlds/QcModLoader.h"

#include <QCommandLineParser>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QStringList>

#include <memory>
#include <optional>

class QcClientSession;
class QcDiagnosticsSession;
class QcDedicatedServer;
class QcHandbook;
class QcContentRegistry;
class QcWorldLibrary;
class QcControlInterface;
class QcLocalServerHost;
class QcStatsLog;
class QFile;

// Server and control-interface client in one process without any window: the base of the
// scenario tests and of external automation. Usage: see `qtcraft-headless --help`.
class QcHeadlessGame : public QObject
{
    Q_OBJECT

public:
    explicit QcHeadlessGame(QObject *parent = nullptr);
    ~QcHeadlessGame() override;
    Q_DISABLE_COPY_MOVE(QcHeadlessGame)

    // Parses the command line and starts; returns false (after printing the reason) on bad options.
    bool start(const QStringList &arguments);

private:
    // Connects the control interface to the bot and feeds it from `scriptFile` or, without one,
    // from standard input.
    bool startControl(const std::optional<QString> &scriptFile);
    // Exits with code 2 after `seconds`.
    void startTimeLimit(int seconds);
    // --join: the bot joins a server over TCP (decision E-270); the control interface starts once
    // the server let it in.
    bool startJoin(const QCommandLineParser &parser, const QcModLoader::Result &content, int viewDistance, int timeLimit);
    // The world in this process with the bot (or the console) on it.
    bool startLocal(const QCommandLineParser &parser, const QcServerConfig &config, int timeLimit,
                    const std::shared_ptr<const QcContentRegistry> &content);
    void onJoined(std::unique_ptr<QcClientSession> session, const std::optional<QString> &scriptFile);
    void readStandardInput();
    // --console: standard input runs server commands (decision E-247); `timeLimit` 0 for none.
    // Without `stopAtEndOfInput` (--no-eof-stop) the server outlives its input.
    void startConsole(int timeLimit, bool stopAtEndOfInput);
    // --stats / QTCRAFT_STATS: the debug data as a JSON line in the log every `intervalMs` (0: off).
    void startStatsLog(int intervalMs);
    void finishScript();
    // --server: a dedicated server without bot (decision E-255).
    bool startDedicated(const QCommandLineParser &parser, const QcModLoader::Result &content);
    // Starts (or restarts) server and bot session for a world; the control interface follows.
    [[nodiscard]] QcStatus startWorld(const QcServerConfig &config);
    // Opens a world and switches the bot to it, replacing the running one only on success.
    [[nodiscard]] QcStatus openWorld(const QcServerConfig &config);
    // The response to a world command (QcControlInterface::ExtensionHandler); none for another command.
    [[nodiscard]] std::optional<QJsonObject> worldCommand(const QJsonObject &request);
    // --join: the handbook; world commands are refused.
    [[nodiscard]] std::optional<QJsonObject> remoteCommand(const QJsonObject &request);
    QJsonArray worldList() const;
    // A world to start and its folder.
    struct WorldChoice
    {
        QcServerConfig config;
        QString folder;
    };
    // The configuration of a new world (`worldCreate`) or a listed one (`worldLoad`).
    [[nodiscard]] QcResult<WorldChoice> createdWorld(const QJsonObject &request) const;
    [[nodiscard]] QcResult<WorldChoice> listedWorld(const QJsonObject &request) const;
    // The `handbook` command: categories, a category's entries, a search or one entry.
    [[nodiscard]] QJsonObject handbookCommand(const QJsonObject &request);
    const QcHandbook &handbook(const QString &language);

    // First, so it is destroyed last: the log stays open until everything else said goodbye.
    std::unique_ptr<QcDiagnosticsSession> m_diagnostics;
    std::unique_ptr<QcDedicatedServer> m_dedicated;
    std::unique_ptr<QcLocalServerHost> m_server;
    std::shared_ptr<const QcContentRegistry> m_content;
    QcServerConfig m_baseConfig;
    QcServerConfig m_currentConfig;
    std::unique_ptr<QcWorldLibrary> m_library;
    std::unique_ptr<QcClientSession> m_session;
    std::unique_ptr<QcControlInterface> m_control;
    QcStatsLog *m_statsLog = nullptr;
    QFile *m_output = nullptr;
    bool m_scriptMode = false;
    // --console: no bot and no control interface, standard input is the server console.
    bool m_consoleMode = false;
    // --join: the bot plays on a server of another process; no world commands.
    bool m_remote = false;
    // Built on first use per language.
    QHash<QString, std::shared_ptr<const QcHandbook>> m_handbooks;
};
