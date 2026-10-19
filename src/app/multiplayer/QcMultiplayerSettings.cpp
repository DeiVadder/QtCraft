#include "app/multiplayer/QcMultiplayerSettings.h"

#include "clientcore/debug/QcClientLogging.h"
#include "common/core/QcNameText.h"
#include "network/QcServerAddress.h"

#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QSettings>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

constexpr int defaultMaxPlayers = 8;

} // namespace

QcMultiplayerSettings::QcMultiplayerSettings(const QString &path, QObject *parent)
    : QObject(parent)
    , m_path(path)
{
    if (!QFileInfo(path).isFile()) {
        return;
    }

    QSettings file(path, QSettings::IniFormat);
    m_playerName = cleanName(file.value(u"multiplayer/playerName"_s).toString());
    m_history = QcServerHistory::load(file);
    bool ok = false;
    const int port = file.value(u"multiplayer/hostPort"_s).toInt(&ok);
    m_hostPort = ok && port >= 0 && port <= 65535 ? port : QcServerAddress::defaultPort;
    const int players = file.value(u"multiplayer/hostMaxPlayers"_s).toInt(&ok);
    m_hostMaxPlayers = ok ? std::clamp(players, 1, maxHostPlayers) : defaultMaxPlayers;
}

QString QcMultiplayerSettings::cleanName(const QString &name) const
{
    return QcNameText::clean(name, maxNameLength);
}

void QcMultiplayerSettings::setPlayerName(const QString &name)
{
    const QString cleaned = cleanName(name);
    if (cleaned == m_playerName) {
        return;
    }

    m_playerName = cleaned;
    save();
    emit changed();
}

QVariantList QcMultiplayerSettings::recentServers() const
{
    QVariantList rows;
    for (const QcServerHistory::Entry &entry : m_history.entries()) {
        const QString when = entry.lastJoined.isValid() ? QLocale().toString(entry.lastJoined.toLocalTime(), QLocale::ShortFormat) : QString();
        rows.append(QVariantMap{{u"address"_s, entry.address}, {u"name"_s, entry.name}, {u"lastJoined"_s, when}});
    }
    return rows;
}

void QcMultiplayerSettings::rememberServer(const QString &address, const QString &serverName)
{
    m_history.add(address, serverName, QDateTime::currentDateTimeUtc());
    save();
    emit changed();
}

void QcMultiplayerSettings::forgetServer(const QString &address)
{
    if (m_history.remove(address)) {
        save();
        emit changed();
    }
}

void QcMultiplayerSettings::rememberHost(int port, int maxPlayers)
{
    m_hostPort = std::clamp(port, 0, 65535);
    m_hostMaxPlayers = std::clamp(maxPlayers, 1, maxHostPlayers);
    save();
    emit changed();
}

void QcMultiplayerSettings::save() const
{
    if (!QDir().mkpath(QFileInfo(m_path).path())) {
        qCWarning(lcSettings) << "Cannot create" << QFileInfo(m_path).path();
        return;
    }

    QSettings file(m_path, QSettings::IniFormat);
    file.setValue(u"multiplayer/playerName"_s, m_playerName);
    file.setValue(u"multiplayer/hostPort"_s, m_hostPort);
    file.setValue(u"multiplayer/hostMaxPlayers"_s, m_hostMaxPlayers);
    m_history.save(file);
    file.sync();
    if (file.status() != QSettings::NoError) {
        qCWarning(lcSettings) << "Cannot write the settings to" << m_path;
    }
}
