#include "app/multiplayer/QcPlayerList.h"

#include "clientcore/session/QcClientSession.h"

#include <algorithm>

using namespace Qt::StringLiterals;

QcPlayerList::QcPlayerList(QObject *parent)
    : QObject(parent)
{
}

void QcPlayerList::follow(QcClientSession *session)
{
    if (m_session) {
        m_session->disconnect(this);
    }

    m_session = session;
    m_entries.clear();
    m_received = false;
    if (m_session) {
        connect(m_session, &QcClientSession::playersChanged, this, &QcPlayerList::update);
        connect(m_session, &QcClientSession::stateChanged, this, &QcPlayerList::changed);
        if (!m_session->players().isEmpty()) {
            update();
        }
    }
    emit changed();
}

void QcPlayerList::update()
{
    const QList<Entry> &now = m_session->players();
    if (m_received) {
        const Changes changes = compare(m_entries, now, m_session->welcome().playerEntityId);
        for (const QString &name : changes.joined) {
            emit announced(tr("%1 joined the game").arg(name));
        }
        for (const QString &name : changes.left) {
            emit announced(tr("%1 left the game").arg(name));
        }
    }

    m_entries = now;
    m_received = true;
    emit changed();
}

QcPlayerList::Changes QcPlayerList::compare(const QList<Entry> &before, const QList<Entry> &after, quint32 ownEntity)
{
    const auto contains = [](const QList<Entry> &list, quint32 entityId) { return std::ranges::find(list, entityId, &Entry::entityId) != list.end(); };

    Changes changes;
    for (const Entry &entry : after) {
        if (entry.entityId != ownEntity && !contains(before, entry.entityId)) {
            changes.joined.append(entry.name);
        }
    }
    for (const Entry &entry : before) {
        if (entry.entityId != ownEntity && !contains(after, entry.entityId)) {
            changes.left.append(entry.name);
        }
    }
    return changes;
}

QVariantList QcPlayerList::players() const
{
    QList<Entry> sorted = m_entries;
    // Spectators come last, as in the reference (decision E-730).
    std::ranges::sort(sorted, [](const Entry &a, const Entry &b) {
        if (a.spectator != b.spectator) {
            return b.spectator;
        }
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });

    const quint32 own = m_session ? m_session->welcome().playerEntityId : 0;
    QVariantList rows;
    for (const Entry &entry : std::as_const(sorted)) {
        rows.append(QVariantMap{{u"name"_s, entry.name},
                                {u"operator"_s, entry.operatorRights},
                                {u"self"_s, own != 0 && entry.entityId == own},
                                {u"spectator"_s, entry.spectator}});
    }
    return rows;
}

QString QcPlayerList::serverName() const
{
    if (!m_session) {
        return {};
    }
    const QString name = m_session->login().serverName();
    return name.isEmpty() ? m_session->welcome().worldName : name;
}

QString QcPlayerList::motd() const
{
    return m_session ? m_session->login().motd() : QString();
}
