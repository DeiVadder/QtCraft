#include "QcCommandSource.h"

#include "QcChatText.h"
#include "QcPhysics.h"
#include "QcServerClient.h"

QcCommandSource::QcCommandSource(QcServerClient &client, QcPermission permission)
    : m_client(&client)
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
    return m_client ? m_client->entityId : 0;
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
    if (m_client) {
        m_client->connection->send(line);
    } else if (m_output) {
        m_output(line);
    }
}
