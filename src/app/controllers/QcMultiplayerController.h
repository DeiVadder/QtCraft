#pragma once

#include "QcLoginMessages.h"
#include "QcMultiplayerSettings.h"
#include "QcPlayerList.h"
#include "QcResult.h"

#include <QHostAddress>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

class QcGameController;
class QcNetworkClient;
class QcSettingsController;
class QcWorldLibraryController;
struct QcSessionParts;

// Network games for QML (GameController.multiplayer, decisions E-251, E-258 to E-261): joining a server by
// address with name and password, opening the running single-player world to the LAN (the local
// player stays in-process), the players on the server and what the screens remember.
class QcMultiplayerController : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    // A join is under way (connecting or logging in).
    Q_PROPERTY(bool joining READ joining NOTIFY joiningChanged FINAL)
    // "connecting", "loggingIn" or "" while no join is under way.
    Q_PROPERTY(QString joinStage READ joinStage NOTIFY joiningChanged FINAL)
    // The address of the last join (for "join again" after a disconnect).
    Q_PROPERTY(QString lastAddress READ lastAddress NOTIFY joiningChanged FINAL)
    // The port the running world listens on; 0 when it is not open to the network.
    Q_PROPERTY(int lanPort READ lanPort NOTIFY lanPortChanged FINAL)
    // The running world is a network server's (joined), not this computer's.
    Q_PROPERTY(bool networkGame READ networkGame NOTIFY networkGameChanged FINAL)
    Q_PROPERTY(QcPlayerList *players READ players CONSTANT FINAL)
    Q_PROPERTY(QcMultiplayerSettings *settings READ settings CONSTANT FINAL)

public:
    static constexpr int defaultPort = 25600;

    QcMultiplayerController(QcGameController &game, QcSessionParts &parts, const QcSettingsController &settings,
                            const QcWorldLibraryController &library, QObject *parent = nullptr);

    bool joining() const;
    QString joinStage() const;
    QString lastAddress() const { return m_lastAddress; }
    int lanPort() const { return m_lanPort; }
    bool networkGame() const { return m_networkGame; }
    QcPlayerList *players() const { return m_players; }
    QcMultiplayerSettings *settings() const { return m_settings; }

    // Connects to "host[:port]" (IPv6 in brackets) and plays there once logged in; the reason of a
    // failure (unreachable, refused with a translated reason) comes with joinFailed(). The name is
    // remembered for the next time, the password only until the program ends (join again).
    Q_INVOKABLE void join(const QString &address, const QString &playerName, const QString &password = {});
    // The last join once more (after a disconnect).
    Q_INVOKABLE void joinAgain();
    Q_INVOKABLE void cancelJoin();
    // Opens the running single-player world to network players on `port` (0: any free one) on
    // every interface; returns the port, or -1 after lanFailed().
    Q_INVOKABLE int openToLan(int port = defaultPort, const QString &password = {}, int maxPlayers = 8);
    Q_INVOKABLE void closeLan();
    // "address:port" for every address other computers may reach this one at while it hosts.
    Q_INVOKABLE QStringList lanAddresses() const;

signals:
    void joiningChanged();
    void lanPortChanged();
    void networkGameChanged();
    void joined();
    void joinFailed(const QString &reason);
    void lanFailed(const QString &reason);

private:
    void onJoined();
    void onSessionStarted();
    void setNetworkGame(bool networkGame);
    // Id, version and hash of the packs this game would load, cached (decision E-277).
    QcResult<QList<QcPackFingerprint>> contentPacks();

    QcGameController &m_game;
    QcSessionParts &m_parts;
    const QcSettingsController &m_gameSettings;
    const QcWorldLibraryController &m_library;
    QcNetworkClient *m_client = nullptr;
    QcPlayerList *m_players = nullptr;
    QcMultiplayerSettings *m_settings = nullptr;
    int m_lanPort = 0;
    QHostAddress m_lanBind;
    bool m_networkGame = false;
    QString m_lastAddress;
    QString m_lastName;
    QString m_lastPassword;
    std::optional<QList<QcPackFingerprint>> m_packs;
    QString m_packsDirectory;
    int m_packsGeneration = -1;
};
