#include "app/multiplayer/QcServerHistory.h"

#include "network/QcServerAddress.h"

#include <QSettings>

#include <ranges>

using namespace Qt::StringLiterals;

namespace {

constexpr auto group = "multiplayer/servers";

} // namespace

QString QcServerHistory::key(const QString &address)
{
    const QcResult<QcServerAddress> parsed = QcServerAddress::parse(address.trimmed());
    return parsed ? parsed->toString().toLower() : QString();
}

void QcServerHistory::add(const QString &address, const QString &name, const QDateTime &when)
{
    const QString entryKey = key(address);
    if (entryKey.isEmpty()) {
        return;
    }

    remove(address);
    m_entries.prepend({address.trimmed().left(maxTextLength), name.trimmed().left(maxTextLength), when});
    if (m_entries.size() > maxEntries) {
        m_entries.resize(maxEntries);
    }
}

bool QcServerHistory::remove(const QString &address)
{
    const QString entryKey = key(address);
    return !entryKey.isEmpty() && m_entries.removeIf([&entryKey](const Entry &entry) { return key(entry.address) == entryKey; }) > 0;
}

QcServerHistory QcServerHistory::load(QSettings &file)
{
    QcServerHistory history;
    const int count = file.beginReadArray(QString::fromLatin1(group));
    QList<Entry> read;
    for (int i = 0; i < count; ++i) {
        file.setArrayIndex(i);
        read.append({file.value(u"address"_s).toString(), file.value(u"name"_s).toString(), file.value(u"lastJoined"_s).toDateTime()});
    }
    file.endArray();

    // Oldest first, so that add() keeps the order and drops duplicates and surplus entries.
    for (const Entry &entry : std::views::reverse(read)) {
        history.add(entry.address, entry.name, entry.lastJoined);
    }
    return history;
}

void QcServerHistory::save(QSettings &file) const
{
    file.remove(QString::fromLatin1(group));
    file.beginWriteArray(QString::fromLatin1(group), static_cast<int>(m_entries.size()));
    for (int i = 0; i < m_entries.size(); ++i) {
        file.setArrayIndex(i);
        file.setValue(u"address"_s, m_entries[i].address);
        file.setValue(u"name"_s, m_entries[i].name);
        file.setValue(u"lastJoined"_s, m_entries[i].lastJoined);
    }
    file.endArray();
}
