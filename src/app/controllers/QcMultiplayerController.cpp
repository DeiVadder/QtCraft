// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/controllers/QcMultiplayerController.h"

#include "app/controllers/QcCharacterSettings.h"
#include "app/controllers/QcSettingsController.h"
#include "app/controllers/QcWorldLibraryController.h"
#include "app/debug/QcAppLogging.h"
#include "app/multiplayer/QcLanAddresses.h"
#include "app/multiplayer/QcMultiplayerSettings.h"
#include "app/multiplayer/QcPlayerList.h"
#include "app/session/QcSessionParts.h"
#include "app/session/QcWorldStarter.h"
#include "clientcore/network/QcKnownServers.h"
#include "clientcore/network/QcNetworkClient.h"
#include "clientcore/session/QcClientSession.h"
#include "common/auth/QcPlayerKey.h"
#include "common/auth/QcProfileValidation.h"
#include "common/core/QcDataRoot.h"
#include "common/protocol/QcLoginSettings.h"
#include "network/QcNetworkConfig.h"
#include "network/tls/QcTlsBackend.h"
#include "network/tls/QcTlsCredentials.h"
#include "worlds/QcContentFingerprint.h"
#include "worlds/QcLocalServerHost.h"
#include "worlds/QcModLoader.h"


using namespace Qt::StringLiterals;

QcMultiplayerController::QcMultiplayerController(QcSessionParts &parts, const QcWorldStarter &worlds, const QcSettingsController &settings,
                                                 const QcWorldLibraryController &library, QObject *parent)
    : QObject(parent)
    , m_parts(parts)
    , m_worlds(worlds)
    , m_gameSettings(settings)
    , m_library(library)
    , m_client(new QcNetworkClient(this))
    , m_players(new QcPlayerList(this))
    , m_settings(new QcMultiplayerSettings(QcSettingsController::settingsPath(), this))
{
    connect(m_client, &QcNetworkClient::stageChanged, this, &QcMultiplayerController::joiningChanged);
    connect(m_client, &QcNetworkClient::joined, this, &QcMultiplayerController::onJoined);
    connect(m_client, &QcNetworkClient::certificateChanged, this, &QcMultiplayerController::setCertificateChangedAddress);
    // OpenSSL with the game's start, so a broken installation shows in the log at once (decision
    // E-872); joining and hosting report the reason again.
    // ensure() logs a failure itself.
    [[maybe_unused]] const QcStatus tls = QcTlsBackend::ensure();
    // QcNetworkClient logged the details (address, addresses tried, steps).
    connect(m_client, &QcNetworkClient::failed, this, [this](const QcJoinFailure &failure) {
        qCInfo(lcGame).noquote() << u"Joining failed: %1"_s.arg(failure.reason);
        failJoin(failure);
    });
}

QcMultiplayerController::~QcMultiplayerController() = default;

void QcMultiplayerController::forget()
{
    m_players->follow(nullptr);
    setNetworkGame(false);
}

std::unique_ptr<QcClientSession> QcMultiplayerController::takeJoinedSession()
{
    return std::move(m_joinedSession);
}

void QcMultiplayerController::sessionStarted()
{
    m_lanPort = 0;
    emit lanPortChanged();
    // Without an in-process server the session belongs to a network game.
    setNetworkGame(!m_parts.server);
    m_players->follow(m_parts.session.get());
}

void QcMultiplayerController::setCertificateChangedAddress(const QString &address)
{
    if (m_certificateChangedAddress != address) {
        m_certificateChangedAddress = address;
        emit certificateChangedAddressChanged();
    }
}

bool QcMultiplayerController::forgetCertificate(const QString &address)
{
    const QcResult<QcServerAddress> parsed = QcServerAddress::parse(address.trimmed());
    if (!parsed) {
        qCWarning(lcGame).noquote() << u"Not forgetting the certificate of \"%1\": not an address"_s.arg(address);
        failJoin(tr("Invalid address: %1").arg(address));
        return false;
    }
    QcKnownServers known(QcKnownServers::defaultPath());
    QcStatus forgotten = known.load();
    if (forgotten) {
        forgotten = known.forget(*parsed);
    }
    if (!forgotten) {
        failJoin(forgotten.error());
        return false;
    }
    qCInfo(lcGame).noquote() << u"Forgot the pinned certificate of %1"_s.arg(parsed->toString());
    setCertificateChangedAddress({});
    return true;
}

void QcMultiplayerController::setJoinFailure(const QcJoinFailure &failure)
{
    if (failure.logText() == m_joinFailure.logText()) {
        return;
    }
    m_joinFailure = failure;
    emit failureChanged();
}

void QcMultiplayerController::setLanFailure(const QString &reason)
{
    if (reason == m_lanFailure) {
        return;
    }
    m_lanFailure = reason;
    emit failureChanged();
}

void QcMultiplayerController::failJoin(const QString &reason)
{
    failJoin(QcJoinFailure{.kind = QcJoinFailure::Kind::Other, .address = {}, .tried = {}, .reason = reason, .detail = {}, .steps = {}});
}

void QcMultiplayerController::failJoin(const QcJoinFailure &failure)
{
    setJoinFailure(failure);
    emit joinFailed(failure.reason);
}

void QcMultiplayerController::failLan(const QString &reason)
{
    setLanFailure(reason);
    emit lanFailed(reason);
}

void QcMultiplayerController::clearFailures()
{
    setJoinFailure({});
    setLanFailure({});
}

void QcMultiplayerController::setNetworkGame(bool networkGame)
{
    if (m_networkGame != networkGame) {
        m_networkGame = networkGame;
        emit networkGameChanged();
    }
}

bool QcMultiplayerController::joining() const
{
    return m_client->stage() != QcNetworkClient::Stage::Idle;
}

QcNetworkClient::Stage QcMultiplayerController::joinStage() const
{
    return m_client->stage();
}

void QcMultiplayerController::join(const QString &address, const QString &playerName, const QString &password)
{
    setJoinFailure({});
    const QString name = m_settings->cleanName(playerName);
    if (name.isEmpty()) {
        qCWarning(lcGame) << "Join refused: no player name in" << playerName;
        failJoin(tr("Enter a player name"));
        return;
    }
    // The server refuses them as well (QcServerLogin::hello).
    if (QcProfileValidation::hasMarkup(name)) {
        qCWarning(lcGame) << "Join refused: the player name" << name << "contains markup";
        failJoin(tr("Player names may not contain <, > or &"));
        return;
    }
    const QcResult<QcServerAddress> parsed = QcServerAddress::parse(address.trimmed());
    if (!parsed) {
        qCWarning(lcGame).noquote() << u"Join refused: \"%1\" is no address (%2)"_s.arg(address, parsed.error());
        failJoin(tr("Invalid address: %1").arg(address));
        return;
    }
    m_settings->setPlayerName(name);
    m_lastAddress = address.trimmed();
    m_lastName = name;
    m_lastPassword = password;
    setCertificateChangedAddress({});
    const QcResult<QByteArray> key = QcPlayerKey::loadOrCreate(QcPlayerKey::defaultPath());
    if (!key) {
        failJoin(key.error());
        return;
    }

    // The server compares its content with the mods this game would load.
    const QcResult<QList<QcPackFingerprint>> packs = contentPacks();
    if (!packs) {
        failJoin(tr("Cannot load game content: %1").arg(packs.error()));
        return;
    }

    QcNetworkClient::Request request;
    request.address = *parsed;
    request.playerName = name;
    request.appearance = m_gameSettings.character()->appearance();
    // One extra ring like in single player: sections are meshed once their neighbours are known.
    request.credentials = {*key, password, *packs, m_gameSettings.viewDistance() + 1};
    request.knownServersFile = QcKnownServers::defaultPath();
    m_client->join(request);
}

bool QcMultiplayerController::canJoin(const QString &address, const QString &playerName)
{
    return !address.trimmed().isEmpty() && !playerName.trimmed().isEmpty();
}

QcResult<QList<QcPackFingerprint>> QcMultiplayerController::contentPacks()
{
    // Loading every mod and hashing its files takes a while on the UI thread: once per mods folder
    // and content generation (mods switched or looked for anew, a world started), not per join.
    const QString directory = m_library.modsDirectory();
    if (m_packs && m_packsDirectory == directory && m_packsGeneration == m_library.contentGeneration()) {
        return *m_packs;
    }

    // The warnings were reported when the content was loaded for the world list or a start.
    const QcModLoader::Loaded content = QcModLoader::load(directory);
    if (!content) {
        return qcError(content.error().error);
    }
    m_packs = QcContentFingerprint::of(*content->content, content->mods);
    m_packsDirectory = directory;
    m_packsGeneration = m_library.contentGeneration();
    return *m_packs;
}

void QcMultiplayerController::joinAgain()
{
    if (!m_lastAddress.isEmpty()) {
        join(m_lastAddress, m_lastName, m_lastPassword);
    }
}

void QcMultiplayerController::cancelJoin()
{
    m_client->cancel();
}

void QcMultiplayerController::onJoined()
{
    if (std::unique_ptr<QcClientSession> session = m_client->takeSession()) {
        m_settings->rememberServer(m_lastAddress, session->login().serverName());
        m_joinedSession = std::move(session);
        emit sessionJoined();
        emit joined();
    }
}

int QcMultiplayerController::openToLan(int port, const QString &password, int maxPlayers, bool ipv6)
{
    setLanFailure({});
    if (!m_parts.server || !m_parts.session || !m_parts.session->content()) {
        failLan(tr("Only a running single-player world can be opened to the network"));
        return -1;
    }

    QcNetworkConfig network;
    network.port = static_cast<quint16>(std::clamp(port, 0, 65535));
    network.bindAddress = ipv6 ? QHostAddress::Any : QHostAddress::AnyIPv4;
    // Test hook: QTCRAFT_LAN_BIND=127.0.0.1 keeps scripted runs on this computer.
    if (const QHostAddress bind(qEnvironmentVariable("QTCRAFT_LAN_BIND")); !bind.isNull()) {
        network.bindAddress = bind;
    }
    m_lanBind = network.bindAddress;
    // The certificate of this installation (decision E-872), made the first time a world is opened.
    QcResult<QcTlsCredentials> tls = QcTlsCredentials::loadOrCreate(QcDataRoot::path());
    if (!tls) {
        failLan(tls.error());
        return -1;
    }
    network.tls = std::move(*tls);
    // Empty in a build without TLS: then there is no certificate.
    m_lanFingerprint = network.tls.isNull() ? QString() : QcTlsFingerprint::display(QcTlsFingerprint::of(network.tls.certificate));
    QcLoginSettings login;
    login.password = password;
    login.maxPlayers = std::max(1, maxPlayers);
    login.contentPacks = QcContentFingerprint::of(*m_parts.session->content(), m_worlds.loadedMods());
    const QcResult<quint16> listening = m_parts.server->listen(network, login);
    if (!listening) {
        failLan(listening.error());
        return -1;
    }

    m_lanPort = *listening;
    m_settings->rememberHost(port, login.maxPlayers);
    emit lanPortChanged();
    return m_lanPort;
}

std::optional<int> QcMultiplayerController::portOf(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return 0;
    }
    bool ok = false;
    const int port = trimmed.toInt(&ok);
    return ok && port >= 0 && port <= 65535 ? std::optional<int>(port) : std::nullopt;
}

int QcMultiplayerController::openToLanAt(const QString &port, const QString &password, int maxPlayers, bool ipv6)
{
    const std::optional<int> number = portOf(port);
    if (!number) {
        qCWarning(lcGame) << "Opening to the network refused: no port in" << port;
        failLan(tr("Invalid port: %1 (a number from 0 to 65535)").arg(port));
        return -1;
    }
    return openToLan(*number, password, maxPlayers, ipv6);
}

void QcMultiplayerController::closeLan()
{
    if (m_parts.server) {
        m_parts.server->stopListening();
    }
    m_lanPort = 0;
    emit lanPortChanged();
}

QStringList QcMultiplayerController::lanAddresses() const
{
    if (m_lanPort <= 0) {
        return {};
    }
    // A listener on one address is reached only there.
    const auto port = static_cast<quint16>(m_lanPort);
    if (m_lanBind == QHostAddress::AnyIPv4) {
        // Bound to IPv4 only: the IPv6 addresses do not reach it.
        QStringList addresses = QcLanAddresses::current(port);
        addresses.removeIf([](const QString &address) { return address.startsWith(u'['); });
        return addresses;
    }
    const bool anyAddress = m_lanBind == QHostAddress::Any || m_lanBind == QHostAddress::AnyIPv6;
    return anyAddress ? QcLanAddresses::current(port) : QcLanAddresses::describe({m_lanBind}, port, true);
}
