// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/multiplayer/QcMultiplayerSettings.h"
#include "app/multiplayer/QcPlayerList.h"
#include "clientcore/network/QcNetworkClient.h"
#include "common/core/QcResult.h"
#include "common/protocol/QcLoginMessages.h"

#include <QHostAddress>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>
#include <optional>

class QcClientSession;
class QcSettingsController;
class QcWorldLibraryController;
class QcWorldStarter;
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
    // NetworkClient.Stage.Connecting or LoggingIn; Idle while no join is under way.
    Q_PROPERTY(QcNetworkClient::Stage joinStage READ joinStage NOTIFY joiningChanged FINAL)
    // The address of the last join (for "join again" after a disconnect).
    Q_PROPERTY(QString lastAddress READ lastAddress NOTIFY joiningChanged FINAL)
    // The port the running world listens on; 0 when it is not open to the network.
    Q_PROPERTY(int lanPort READ lanPort NOTIFY lanPortChanged FINAL)
    // The running world is a network server's (joined), not this computer's.
    Q_PROPERTY(bool networkGame READ networkGame NOTIFY networkGameChanged FINAL)
    // The address whose server showed another certificate than the pinned one at the last join
    // (decision E-872); empty otherwise. forgetCertificate() drops the pin.
    Q_PROPERTY(QString certificateChangedAddress READ certificateChangedAddress NOTIFY certificateChangedAddressChanged FINAL)
    // The fingerprint of the certificate players pin while the running world is open to the
    // network, for the host to read out ("AB12 CD34 …"); empty when it is not open.
    Q_PROPERTY(QString lanFingerprint READ lanFingerprint NOTIFY lanPortChanged FINAL)
    Q_PROPERTY(QcPlayerList *players READ players CONSTANT FINAL)
    Q_PROPERTY(QcMultiplayerSettings *settings READ settings CONSTANT FINAL)

public:
    static constexpr int defaultPort = 25600;

    // `worlds` knows the mods of the running world, announced to players joining it; all
    // references outlive this object.
    QcMultiplayerController(QcSessionParts &parts, const QcWorldStarter &worlds, const QcSettingsController &settings,
                            const QcWorldLibraryController &library, QObject *parent = nullptr);
    ~QcMultiplayerController() override;
    Q_DISABLE_COPY_MOVE(QcMultiplayerController)

    bool joining() const;
    QcNetworkClient::Stage joinStage() const;
    QString lastAddress() const { return m_lastAddress; }
    int lanPort() const { return m_lanPort; }
    bool networkGame() const { return m_networkGame; }
    QString certificateChangedAddress() const { return m_certificateChangedAddress; }
    QString lanFingerprint() const { return m_lanPort > 0 ? m_lanFingerprint : QString(); }
    QcPlayerList *players() const { return m_players; }
    QcMultiplayerSettings *settings() const { return m_settings; }

    // Connects to "host[:port]" (IPv6 in brackets) and plays there once logged in; the reason of a
    // failure (unreachable, refused with a translated reason) comes with joinFailed(). The name is
    // remembered for the next time, the password only until the program ends (join again).
    Q_INVOKABLE void join(const QString &address, const QString &playerName, const QString &password = {});
    // The last join once more (after a disconnect).
    Q_INVOKABLE void joinAgain();
    Q_INVOKABLE void cancelJoin();
    // Drops the pinned certificate of "host[:port]" (QcKnownServers): the next join pins what the
    // server shows then. False with joinFailed() when the list cannot be written.
    Q_INVOKABLE bool forgetCertificate(const QString &address);
    // Opens the running single-player world to network players on `port` (0: any free one) on
    // every interface, over IPv4 only unless `ipv6` (decision E-872: an IPv6 address is often
    // reachable from the internet); returns the port, or -1 after lanFailed().
    Q_INVOKABLE int openToLan(int port = defaultPort, const QString &password = {}, int maxPlayers = 8, bool ipv6 = false);
    Q_INVOKABLE void closeLan();
    // "address:port" for every address other computers may reach this one at while it hosts.
    Q_INVOKABLE QStringList lanAddresses() const;

    // A session started (single-player or joined): the world is not open to the network yet.
    void sessionStarted();
    // Back in the menu: the players and the network game are forgotten.
    void forget();
    // The logged-in session of the server joined last (sessionJoined()), once; nullptr afterwards.
    [[nodiscard]] std::unique_ptr<QcClientSession> takeJoinedSession();

signals:
    void joiningChanged();
    void lanPortChanged();
    void networkGameChanged();
    void certificateChangedAddressChanged();
    // A join succeeded: the game takes the session (takeJoinedSession()) and plays its world;
    // joined() follows.
    void sessionJoined();
    void joined();
    void joinFailed(const QString &reason);
    void lanFailed(const QString &reason);

private:
    void onJoined();
    void setNetworkGame(bool networkGame);
    void setCertificateChangedAddress(const QString &address);
    // Id, version and hash of the packs this game would load, cached (decision E-277).
    [[nodiscard]] QcResult<QList<QcPackFingerprint>> contentPacks();

    QcSessionParts &m_parts;
    const QcWorldStarter &m_worlds;
    const QcSettingsController &m_gameSettings;
    const QcWorldLibraryController &m_library;
    QcNetworkClient *m_client = nullptr;
    std::unique_ptr<QcClientSession> m_joinedSession;
    QcPlayerList *m_players = nullptr;
    QcMultiplayerSettings *m_settings = nullptr;
    int m_lanPort = 0;
    QHostAddress m_lanBind;
    bool m_networkGame = false;
    QString m_lastAddress;
    QString m_lastName;
    QString m_lastPassword;
    QString m_certificateChangedAddress;
    QString m_lanFingerprint;
    std::optional<QList<QcPackFingerprint>> m_packs;
    QString m_packsDirectory;
    int m_packsGeneration = -1;
};
