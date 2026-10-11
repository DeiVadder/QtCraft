#include "QcMultiplayerController.h"

#include "QcCharacterSettings.h"
#include "QcClientSession.h"
#include "QcContentFingerprint.h"
#include "QcGameController.h"
#include "QcLocalServerHost.h"
#include "QcLanAddresses.h"
#include "QcModLoader.h"
#include "QcMultiplayerSettings.h"
#include "QcNetworkClient.h"
#include "QcPlayerKey.h"
#include "QcPlayerList.h"
#include "QcProfileValidation.h"
#include "QcSessionParts.h"
#include "QcSettingsController.h"
#include "QcWorldLibraryController.h"

#include <QLoggingCategory>

using namespace Qt::StringLiterals;

namespace {

Q_LOGGING_CATEGORY(lcMultiplayer, "qtcraft.game")

} // namespace

QcMultiplayerController::QcMultiplayerController(QcGameController &game, QcSessionParts &parts, const QcSettingsController &settings,
                                                 const QcWorldLibraryController &library, QObject *parent)
    : QObject(parent)
    , m_game(game)
    , m_parts(parts)
    , m_gameSettings(settings)
    , m_library(library)
    , m_client(new QcNetworkClient(this))
    , m_players(new QcPlayerList(this))
    , m_settings(new QcMultiplayerSettings(QcSettingsController::settingsPath(), this))
{
    connect(m_client, &QcNetworkClient::stageChanged, this, &QcMultiplayerController::joiningChanged);
    connect(m_client, &QcNetworkClient::joined, this, &QcMultiplayerController::onJoined);
    connect(m_client, &QcNetworkClient::failed, this, [this](const QString &reason) {
        qCInfo(lcMultiplayer).noquote() << u"Joining failed: %1"_s.arg(reason);
        emit joinFailed(reason);
    });
    connect(&game, &QcGameController::sessionStarted, this, &QcMultiplayerController::onSessionStarted);
    connect(&game, &QcGameController::phaseChanged, this, [this] {
        if (m_game.phase() == u"menu"_s) {
            m_players->follow(nullptr);
            setNetworkGame(false);
        }
    });
}

void QcMultiplayerController::onSessionStarted()
{
    m_lanPort = 0;
    emit lanPortChanged();
    // Without an in-process server the session belongs to a network game.
    setNetworkGame(!m_parts.server);
    m_players->follow(m_parts.session);
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

QString QcMultiplayerController::joinStage() const
{
    switch (m_client->stage()) {
    case QcNetworkClient::Stage::Connecting:
        return u"connecting"_s;
    case QcNetworkClient::Stage::LoggingIn:
        return u"loggingIn"_s;
    case QcNetworkClient::Stage::Idle:
        break;
    }
    return {};
}

void QcMultiplayerController::join(const QString &address, const QString &playerName, const QString &password)
{
    const QString name = m_settings->cleanName(playerName);
    if (name.isEmpty()) {
        emit joinFailed(tr("Enter a player name"));
        return;
    }
    // The server refuses them as well (QcServerLogin::hello).
    if (QcProfileValidation::hasMarkup(name)) {
        emit joinFailed(tr("Player names may not contain <, > or &"));
        return;
    }
    const QcResult<QcServerAddress> parsed = QcServerAddress::parse(address.trimmed());
    if (!parsed) {
        emit joinFailed(tr("Invalid address: %1").arg(address));
        return;
    }
    m_settings->setPlayerName(name);
    m_lastAddress = address.trimmed();
    m_lastName = name;
    m_lastPassword = password;
    const QcResult<QByteArray> key = QcPlayerKey::loadOrCreate(QcPlayerKey::defaultPath());
    if (!key) {
        emit joinFailed(key.error());
        return;
    }

    // The server compares its content with the mods this game would load.
    const QcResult<QList<QcPackFingerprint>> packs = contentPacks();
    if (!packs) {
        emit joinFailed(tr("Cannot load game content: %1").arg(packs.error()));
        return;
    }

    QcNetworkClient::Request request;
    request.address = *parsed;
    request.playerName = name;
    request.appearance = m_gameSettings.character()->appearance();
    // One extra ring like in single player: sections are meshed once their neighbours are known.
    request.credentials = {*key, password, *packs, m_gameSettings.viewDistance() + 1};
    m_client->join(request);
}

QcResult<QList<QcPackFingerprint>> QcMultiplayerController::contentPacks()
{
    // Loading every mod and hashing its files takes a while on the UI thread: once per mods folder
    // and content generation (mods switched or looked for anew, a world started), not per join.
    const QString directory = m_library.modsDirectory();
    if (m_packs && m_packsDirectory == directory && m_packsGeneration == m_library.contentGeneration()) {
        return *m_packs;
    }

    QStringList warnings;
    const QcResult<QcModLoader::Result> content = QcModLoader::load(directory, warnings);
    if (!content) {
        return qcError(content.error());
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
    if (QcClientSession *session = m_client->takeSession()) {
        m_settings->rememberServer(m_lastAddress, session->login().serverName());
        m_game.startRemoteSession(session);
        emit joined();
    }
}

int QcMultiplayerController::openToLan(int port, const QString &password, int maxPlayers)
{
    if (!m_parts.server || !m_parts.session || !m_parts.session->content()) {
        emit lanFailed(tr("Only a running single-player world can be opened to the network"));
        return -1;
    }

    QcNetworkConfig network;
    network.port = quint16(std::clamp(port, 0, 65535));
    // Test hook: QTCRAFT_LAN_BIND=127.0.0.1 keeps scripted runs on this computer.
    if (const QHostAddress bind(qEnvironmentVariable("QTCRAFT_LAN_BIND")); !bind.isNull()) {
        network.bindAddress = bind;
    }
    m_lanBind = network.bindAddress;
    QcLoginSettings login;
    login.password = password;
    login.maxPlayers = std::max(1, maxPlayers);
    login.contentPacks = QcContentFingerprint::of(*m_parts.session->content(), m_game.loadedMods());
    const QcResult<quint16> listening = m_parts.server->listen(network, login);
    if (!listening) {
        emit lanFailed(listening.error());
        return -1;
    }

    m_lanPort = *listening;
    m_settings->rememberHost(port, login.maxPlayers);
    emit lanPortChanged();
    return m_lanPort;
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
    const bool anyAddress = m_lanBind == QHostAddress::Any || m_lanBind == QHostAddress::AnyIPv4 || m_lanBind == QHostAddress::AnyIPv6;
    return anyAddress ? QcLanAddresses::current(quint16(m_lanPort)) : QcLanAddresses::describe({m_lanBind}, quint16(m_lanPort), true);
}
