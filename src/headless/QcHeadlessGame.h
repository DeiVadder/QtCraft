#pragma once

#include "QcResult.h"
#include "QcServerConfig.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QStringList>

#include <memory>
#include <optional>

class QcClientSession;
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

    // Parses the command line and starts; returns false (after printing the reason) on bad options.
    bool start(const QStringList &arguments);

private:
    // Connects the control interface to the bot and feeds it from `scriptFile` or, without one,
    // from standard input; exits with failure after `timeLimit` seconds.
    bool startControl(const std::optional<QString> &scriptFile, int timeLimit);
    void readStandardInput();
    // --stats / QTCRAFT_STATS: the debug data as a JSON line in the log every `intervalMs` (0: off).
    void startStatsLog(int intervalMs);
    void finishScript();
    // Starts (or restarts) server and bot session for a world; the control interface follows.
    QcStatus startWorld(const QcServerConfig &config);
    // Opens a world and switches the bot to it, replacing the running one only on success.
    QcStatus openWorld(const QcServerConfig &config);
    bool handleWorldCommand(const QJsonObject &request, QJsonObject &response);
    QJsonArray worldList() const;
    // The configuration of a new world (`worldCreate`) or a listed one (`worldLoad`); sets the
    // response's folder.
    QcResult<QcServerConfig> createdWorld(const QJsonObject &request, QJsonObject &response) const;
    QcResult<QcServerConfig> listedWorld(const QJsonObject &request, QJsonObject &response) const;
    // The `handbook` command: categories, a category's entries, a search or one entry.
    void handleHandbookCommand(const QJsonObject &request, QJsonObject &response);
    const QcHandbook &handbook(const QString &language);

    std::unique_ptr<QcLocalServerHost> m_server;
    std::shared_ptr<const QcContentRegistry> m_content;
    QcServerConfig m_baseConfig;
    QcServerConfig m_currentConfig;
    std::unique_ptr<QcWorldLibrary> m_library;
    QcClientSession *m_session = nullptr;
    QcControlInterface *m_control = nullptr;
    QcStatsLog *m_statsLog = nullptr;
    QFile *m_output = nullptr;
    bool m_scriptMode = false;
    // Built on first use per language.
    QHash<QString, std::shared_ptr<const QcHandbook>> m_handbooks;
};
