#include "server/commands/QcCommandSource.h"

#include "common/physics/QcPhysics.h"
#include "common/protocol/QcChatText.h"
#include "server/core/QcServerClient.h"

QcCommandSource::QcCommandSource(QcServerClient &client, QcPermission permission)
    : m_client(&client)
    , m_permission(permission)
{
}

QcCommandSource::QcCommandSource(QcServerClient &client, QcPermission permission, Output output)
    : m_client(&client)
    , m_output(std::move(output))
    , m_permission(permission)
{
}

QcCommandSource::QcCommandSource(Output output)
    : m_output(std::move(output))
    , m_permission(QcPermission::Console)
{
}

QString QcCommandSource::name() const
{
    return m_client ? m_client->name : consoleName.toString();
}

quint32 QcCommandSource::id() const
{
    return m_client ? m_client->id : 0;
}

std::optional<QcBlockPos> QcCommandSource::origin() const
{
    if (!m_client) {
        return std::nullopt;
    }
    return QcPhysics::blockContaining(m_client->motion.position);
}

void QcCommandSource::reply(const char *key, const QStringList &arguments) const
{
    send(QcChatText::message(key, arguments));
}

void QcCommandSource::replyPlain(const QString &line) const
{
    send(QcChatText::plain(line));
}

void QcCommandSource::send(const QcMsg::ChatMessage &line) const
{
    if (m_output) {
        m_output(line);
    } else if (m_client) {
        m_client->connection->send(line);
    }
}
