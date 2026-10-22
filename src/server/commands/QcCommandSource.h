#pragma once

#include "common/core/QcBlockPos.h"
#include "common/protocol/QcSessionMessages.h"
#include "server/commands/QcPermission.h"

#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

struct QcServerClient;

// Who runs a command (decision E-243): a player through the chat or the server console. Replies go
// back to the source as translatable chat lines (decision E-094); the console receives the lines
// and shows their English text.
class QcCommandSource
{
public:
    using Output = std::function<void(const QcMsg::ChatMessage &line)>;

    // The name the console has in the log and in messages to players.
    static constexpr QStringView consoleName = u"Server";

    QcCommandSource(QcServerClient &client, QcPermission permission);
    // A player whose replies go to `output` instead of his chat (the admin channel, decision E-848).
    QcCommandSource(QcServerClient &client, QcPermission permission, Output output);
    explicit QcCommandSource(Output output);

    QString name() const;
    QcPermission permission() const { return m_permission; }
    bool may(QcPermission needed) const { return qcAllows(m_permission, needed); }
    // The player behind the source; nullptr for the console.
    QcServerClient *player() const { return m_client; }
    // Key of the source's pending confirmation: the player's entity id, 0 for the console.
    quint32 id() const;
    // The block the player stands in, origin of ~ coordinates; std::nullopt for the console.
    [[nodiscard]] std::optional<QcBlockPos> origin() const;

    // `key` is marked with QT_TRANSLATE_NOOP("QcServerMessages", ...).
    void reply(const char *key, const QStringList &arguments = {}) const;
    void replyPlain(const QString &line) const;
    void send(const QcMsg::ChatMessage &line) const;

private:
    QcServerClient *m_client = nullptr;
    Output m_output;
    QcPermission m_permission = QcPermission::Player;
};
