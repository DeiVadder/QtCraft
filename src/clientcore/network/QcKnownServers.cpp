// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/network/QcKnownServers.h"

#include "common/auth/QcSecretFile.h"
#include "common/core/QcDataRoot.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace Qt::StringLiterals;

namespace {

constexpr int formatVersion = 1;
// A SHA-256 digest in hex.
constexpr qsizetype fingerprintLength = 64;

bool validFingerprint(const QByteArray &hex)
{
    return hex.size() == fingerprintLength && QByteArray::fromHex(hex).toHex() == hex;
}

} // namespace

QString QcKnownServers::defaultPath()
{
    return QDir(QcDataRoot::path()).filePath(QString::fromLatin1(fileName));
}

QString QcKnownServers::keyOf(const QcServerAddress &address)
{
    // An address in Qt's canonical form ("2a02:db8::1" for "2A02:0DB8:0::1"), so one IPv6 address
    // written in several ways is one server; a host name in lower case.
    const QHostAddress literal(address.host);
    const QString host = literal.isNull() ? address.host.toLower() : literal.toString();
    return QcServerAddress{host, address.port}.toString();
}

QcKnownServers::QcKnownServers(QString path)
    : m_path(std::move(path))
{
}

QcStatus QcKnownServers::load()
{
    m_entries.clear();
    QFile file(m_path);
    if (!file.exists()) {
        return {};
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return qcError(u"Cannot read the known servers %1: %2"_s.arg(m_path, file.errorString()));
    }

    const QString damaged = u"The list of known servers %1 is damaged; move it away (every server then counts as new)"_s.arg(m_path);
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject() || document.object().value("format"_L1).toInt() != formatVersion) {
        return qcError(damaged);
    }
    const QJsonArray servers = document.object().value("servers"_L1).toArray();
    for (const auto &value : servers) {
        const QJsonObject server = value.toObject();
        const QString address = server.value("address"_L1).toString();
        const QByteArray fingerprint = server.value("fingerprint"_L1).toString().toLatin1();
        if (address.isEmpty() || !validFingerprint(fingerprint)) {
            return qcError(damaged);
        }
        m_entries.insert(address, {fingerprint, QDateTime::fromString(server.value("pinned"_L1).toString(), Qt::ISODate)});
    }
    return {};
}

QcKnownServers::Trust QcKnownServers::check(const QcServerAddress &address, const QByteArray &fingerprint) const
{
    const std::optional<Entry> entry = find(address);
    if (!entry) {
        return Trust::New;
    }
    return entry->fingerprint == fingerprint ? Trust::Known : Trust::Changed;
}

std::optional<QcKnownServers::Entry> QcKnownServers::find(const QcServerAddress &address) const
{
    const auto found = m_entries.constFind(keyOf(address));
    if (found == m_entries.cend()) {
        return std::nullopt;
    }
    return *found;
}

QcStatus QcKnownServers::remember(const QcServerAddress &address, const QByteArray &fingerprint)
{
    m_entries.insert(keyOf(address), {fingerprint, QDateTime::currentDateTimeUtc()});
    return save();
}

QcStatus QcKnownServers::forget(const QcServerAddress &address)
{
    if (m_entries.remove(keyOf(address)) == 0) {
        return {};
    }
    return save();
}

QcStatus QcKnownServers::save() const
{
    QJsonArray servers;
    QStringList addresses = m_entries.keys();
    addresses.sort();
    for (const QString &address : std::as_const(addresses)) {
        const Entry &entry = m_entries[address];
        servers.append(QJsonObject{{"address"_L1, address},
                                   {"fingerprint"_L1, QString::fromLatin1(entry.fingerprint)},
                                   {"pinned"_L1, entry.pinned.toString(Qt::ISODate)}});
    }
    const QJsonObject root{{"format"_L1, formatVersion}, {"servers"_L1, servers}};

    QDir().mkpath(QFileInfo(m_path).absolutePath());
    if (const QcStatus written = QcSecretFile::write(m_path, QJsonDocument(root).toJson()); !written) {
        return qcError(u"Cannot write the known servers %1: %2"_s.arg(m_path, written.error()));
    }
    return {};
}
