#include "QcIdentityStore.h"

#include "QcAccessList.h"
#include "QcLoginCrypto.h"
#include "QcLoginMessages.h"
#include "QcProfileValidation.h"
#include "QcSaveFile.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSaveFile>

using namespace Qt::StringLiterals;

namespace {

Q_LOGGING_CATEGORY(lcIdentities, "qtcraft.server")

} // namespace

QcIdentityStore::QcIdentityStore()
    : m_salt(QcLoginCrypto::randomBytes(QcMsg::LoginChallenge::saltBytes))
{
}

void QcIdentityStore::open(const QString &directory, QStringList &warnings)
{
    m_path = directory.isEmpty() ? QString() : QDir(directory).filePath(fileName.toString());
    if (!m_path.isEmpty() && QFile::exists(m_path)) {
        read(m_path, warnings);
    }
}

void QcIdentityStore::read(const QString &path, QStringList &warnings)
{
    QFile file(path);
    QJsonParseError error{};
    QJsonDocument document;
    if (file.open(QIODevice::ReadOnly) && file.size() <= maxFileBytes) {
        document = QJsonDocument::fromJson(file.readAll(), &error);
    }
    file.close();

    const QJsonObject root = document.object();
    const QByteArray salt = QByteArray::fromBase64(root.value(u"salt"_s).toString().toLatin1());
    if (!document.isObject() || error.error != QJsonParseError::NoError || root.value(u"format"_s).toInt() != formatVersion
        || salt.size() != QcMsg::LoginChallenge::saltBytes) {
        const QString copy = QcSaveFile::keepAside(path);
        m_readOnly = copy.isEmpty();
        warnings.append(copy.isEmpty() ? u"%1 is damaged and could not be copied aside; players register anew and it is not overwritten"_s.arg(path)
                                       : u"%1 was damaged and kept as %2; players register anew"_s.arg(path, copy));
        return;
    }

    m_salt = salt;
    const QJsonArray players = root.value(u"players"_s).toArray();
    for (const auto &item : players) {
        const QJsonObject object = item.toObject();
        const Identity identity{object.value(u"name"_s).toString(), QByteArray::fromBase64(object.value(u"identity"_s).toString().toLatin1()), {},
                                QDateTime::fromString(object.value(u"since"_s).toString(), Qt::ISODate)};
        if (QcProfileValidation::validPlayerName(identity.name) != identity.name || identity.identity.size() != QcLoginCrypto::keyBytes) {
            warnings.append(u"identities.json: invalid entry left out"_s);
        } else if (m_identities.size() >= maxEntries) {
            warnings.append(u"identities.json: more than %1 entries, the rest is left out"_s.arg(maxEntries));
            return;
        } else {
            m_identities.insert(QcAccessList::key(identity.name), {identity.name, identity.identity, QcLoginCrypto::fingerprint(identity.identity), identity.since});
        }
    }
}

const QcIdentityStore::Identity *QcIdentityStore::find(const QString &name) const
{
    const auto found = m_identities.constFind(QcAccessList::key(name));
    return found != m_identities.constEnd() ? &*found : nullptr;
}

QString QcIdentityStore::fingerprintOf(const QString &name) const
{
    const Identity *identity = find(name);
    return identity ? identity->fingerprint : QString();
}

bool QcIdentityStore::add(const QString &name, const QByteArray &identity, const QDateTime &now)
{
    if (find(name) || isFull() || identity.size() != QcLoginCrypto::keyBytes) {
        return false;
    }

    m_identities.insert(QcAccessList::key(name), {name, identity, QcLoginCrypto::fingerprint(identity), now});
    m_dirty = true;
    return true;
}

QcStatus QcIdentityStore::flush()
{
    if (!m_dirty) {
        return {};
    }

    // One attempt per batch of additions: a file that cannot be written does not fill the log.
    m_dirty = false;
    if (const QcStatus written = write(); !written) {
        qCWarning(lcIdentities).noquote() << written.error();
        return written;
    }
    return {};
}

QcStatus QcIdentityStore::write() const
{
    if (m_path.isEmpty()) {
        return {};
    }
    if (m_readOnly) {
        return qcError(u"%1 is damaged and could not be copied aside; it is kept until it is repaired"_s.arg(m_path));
    }

    QJsonArray players;
    for (const Identity &identity : m_identities) {
        players.append(QJsonObject{{u"name"_s, identity.name},
                                   {u"identity"_s, QString::fromLatin1(identity.identity.toBase64())},
                                   {u"since"_s, identity.since.toString(Qt::ISODate)}});
    }
    const QJsonObject root{{u"format"_s, formatVersion}, {u"salt"_s, QString::fromLatin1(m_salt.toBase64())}, {u"players"_s, players}};

    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(root).toJson()) < 0) {
        return qcError(u"Cannot write %1: %2"_s.arg(m_path, file.errorString()));
    }
    // The identities are secrets of the players on this server.
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (!file.commit()) {
        return qcError(u"Cannot write %1: %2"_s.arg(m_path, file.errorString()));
    }
    return {};
}
