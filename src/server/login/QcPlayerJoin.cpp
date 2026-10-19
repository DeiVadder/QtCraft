#include "server/login/QcPlayerJoin.h"

#include "common/protocol/QcChatText.h"
#include "common/protocol/QcProtocol.h"
#include "server/chunks/QcChunkStreaming.h"
#include "server/core/QcServerLogging.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcPlayerHealth.h"
#include "server/gameplay/QcServerGameModes.h"
#include "server/gameplay/QcServerInventory.h"
#include "server/login/QcServerLogin.h"
#include "server/storage/QcPlayerRecords.h"
#include "server/storage/QcServerPersistence.h"


#include <algorithm>

QcPlayerJoin::QcPlayerJoin(QcServerState &state, Parts parts)
    : m_state(state)
    , m_parts(std::move(parts))
{
}

void QcPlayerJoin::join(QcServerClient &client)
{
    if (restore(client)) {
        return;
    }

    client.state = QcServerClient::State::Playing;
    client.joined = true;
    client.motion.position = m_state.spawnPoint().position();
    client.fallPeak = client.motion.position.y;
    client.gameMode = m_parts.gameModes.defaultMode();
    QcServerGameModes::applyRules(client);
    client.stateDirty = true;
    m_state.notePlayerListChanged();

    qCInfo(lcServer) << "Player" << client.name << "joined as entity" << client.id;
    sendWelcome(client, false);
    m_parts.streaming.stream(client);
}

bool QcPlayerJoin::restore(QcServerClient &client)
{
    if (!m_parts.persistence.hasStorage()) {
        return false;
    }

    QStringList warnings;
    const QcResult<std::optional<QcPlayerData>> stored = m_parts.persistence.readPlayer(client.name, warnings);
    for (const QString &warning : std::as_const(warnings)) {
        qCWarning(lcServer) << warning;
    }

    if (!stored) {
        QcServerLogin::refuse(client, QcChatText::disconnect(QT_TRANSLATE_NOOP("QcServerMessages", "Cannot load player data: %1"), {stored.error()}));
        return true;
    }

    if (!*stored) {
        return false;
    }

    QcPlayerRecords::restore(client, **stored);

    // Who left while dead comes back to the death screen and respawns from there.
    client.dead = client.health <= 0.0;
    client.state = QcServerClient::State::Playing;
    client.joined = true;
    client.stateDirty = true;
    m_state.notePlayerListChanged();

    qCInfo(lcServer) << "Player" << client.name << "rejoined as entity" << client.id;
    sendWelcome(client, true);
    m_state.world().loadChunk(QcChunkStreaming::chunkOf(client.motion.position));
    m_parts.streaming.stream(client);

    return true;
}

void QcPlayerJoin::sendWelcome(QcServerClient &client, bool rejoined)
{
    client.connection->send(QcMsg::Welcome{QcProtocol::version, client.id, m_state.config().worldName, m_state.config().seed,
                                           rejoined ? client.yaw : 0.0f, rejoined ? client.pitch : 0.0f, rejoined,
                                           std::clamp(client.view.distance, 0, QcMsg::Welcome::maxViewDistance)});
    client.connection->send(QcMsg::ContentSync{m_state.contentPointer()});
    QcServerGameModes::send(client);
    m_parts.inventory.send(client);
    m_parts.health.sendHealth(client);
    m_parts.health.sendFood(client);
    m_parts.sendTime(client);
    sendOpenWarnings(client);
    sendAccessWarning(client);
}

void QcPlayerJoin::sendAccessWarning(QcServerClient &client)
{
    // Operators (the host among them) should repair the lists of a damaged access.json (E-244).
    if (!m_state.access().wasDamaged() || m_state.permissionOf(client) == QcPermission::Player) {
        return;
    }

    if (m_state.access().damagedCopy().isEmpty()) {
        client.connection->send(QcChatText::message(QT_TRANSLATE_NOOP(
            "QcServerMessages", "access.json is damaged and could not be copied aside: operators, bans and the whitelist start empty "
                                "with the whitelist on, and changes are not saved until the file is repaired")));
    } else {
        client.connection->send(QcChatText::message(
            QT_TRANSLATE_NOOP("QcServerMessages", "access.json was damaged: operators, bans and the whitelist start empty with the "
                                                  "whitelist on; the damaged file was kept as %1"),
            {m_state.access().damagedCopy()}));
    }
}

void QcPlayerJoin::sendOpenWarnings(QcServerClient &client) const
{
    // The player should know why blocks or items of a removed mod are gone.
    constexpr qsizetype shown = 5;
    const QStringList first = m_parts.persistence.openWarnings().first(std::min(shown, m_parts.persistence.openWarnings().size()));

    for (const QString &warning : first) {
        client.connection->send(QcChatText::plain(warning));
    }

    if (m_parts.persistence.openWarnings().size() > shown) {
        client.connection->send(QcChatText::message(QT_TRANSLATE_NOOP("QcServerMessages", "... and %1 more warnings in the log"),
            {QString::number(m_parts.persistence.openWarnings().size() - shown)}));
    }
}
