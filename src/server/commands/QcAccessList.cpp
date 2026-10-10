#include "QcAccessList.h"

#include "QcProfileValidation.h"
#include "QcStandStorage.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSaveFile>

using namespace Qt::StringLiterals;

namespace {

Q_LOGGING_CATEGORY(lcAccess, "qtcraft.server")

QJsonArray namesToJson(const QMap<QString, QString> &names)
{
    QJsonArray array;
    for (const QString &name : names) {
        array.append(name);
    }
    return array;
}

} // namespace

QString QcAccessList::key(const QString &name)
{
    return QcProfileValidation::playerName(name).toCaseFolded();
}

void QcAccessList::open(const QString &directory, const QStringList &initialOperators, QStringList &warnings)
{
    m_path = directory.isEmpty() ? QString() : QDir(directory).filePath(fileName.toString());
    if (!m_path.isEmpty() && QFile::exists(m_path)) {
        read(m_path, warnings);
    }

    for (const QString &name : initialOperators) {
        m_configOperators.insert(key(name), QcProfileValidation::playerName(name));
    }
}

bool QcAccessList::isFullFor(List list, const QString &name) const
{
    const QString nameKey = key(name);
    switch (list) {
    case List::Operators:
        return m_operators.size() >= maxEntries && !m_operators.contains(nameKey);
    case List::Bans:
        return m_bans.size() >= maxEntries && !m_bans.contains(nameKey);
    case List::Whitelist:
        return m_whitelist.size() >= maxEntries && !m_whitelist.contains(nameKey);
    }
    return true;
}

QStringList QcAccessList::operators() const
{
    QMap<QString, QString> all = m_configOperators;
    all.insert(m_operators);
    return all.values();
}

void QcAccessList::read(const QString &path, QStringList &warnings)
{
    QFile file(path);
    QJsonParseError error{};
    QJsonDocument document;
    if (file.open(QIODevice::ReadOnly) && file.size() <= maxFileBytes) {
        document = QJsonDocument::fromJson(file.readAll(), &error);
    }
    file.close();

    const QJsonObject root = document.object();
    if (!document.isObject() || error.error != QJsonParseError::NoError || root.value(u"format"_s).toInt() != formatVersion) {
        openDamaged(path);
        return;
    }

    readNames(root.value(u"operators"_s), m_operators, u"operators"_s, warnings);
    readBans(root.value(u"bans"_s), warnings);
    const QJsonObject whitelist = root.value(u"whitelist"_s).toObject();
    m_whitelistEnabled = whitelist.value(u"enabled"_s).toBool(false);
    readNames(whitelist.value(u"names"_s), m_whitelist, u"whitelist"_s, warnings);
}

void QcAccessList::openDamaged(const QString &path)
{
    // Fail closed: nobody but operators, the local player and the console joins until it is fixed.
    // Operators hear of it when they join (QcServer::sendWelcome).
    m_damaged = true;
    m_damagedCopy = QcStandStorage::keepAside(path);
    m_whitelistEnabled = true;
    if (m_damagedCopy.isEmpty()) {
        qCWarning(lcAccess).noquote() << u"%1 is damaged and could not be copied aside: operators, bans and the whitelist start "
                                         u"empty with the whitelist on, and changes are not saved until the file is repaired"_s.arg(path);
    } else {
        qCWarning(lcAccess).noquote() << u"%1 is damaged: operators, bans and the whitelist start empty with the whitelist on; "
                                         u"the damaged file was kept as %2"_s.arg(path, m_damagedCopy);
    }
}

void QcAccessList::readNames(const QJsonValue &value, QMap<QString, QString> &names, const QString &list, QStringList &warnings)
{
    const QJsonArray array = value.toArray();
    for (const auto &entry : array) {
        const QString name = entry.toString();
        if (name.isEmpty() || QcProfileValidation::playerName(name) != name) {
            warnings.append(u"access.json: invalid name in %1 left out"_s.arg(list));
        } else if (names.size() >= maxEntries) {
            warnings.append(u"access.json: more than %1 entries in %2, the rest is left out"_s.arg(maxEntries).arg(list));
            return;
        } else {
            names.insert(key(name), name);
        }
    }
}

void QcAccessList::readBans(const QJsonValue &value, QStringList &warnings)
{
    const QJsonArray array = value.toArray();
    for (const auto &entry : array) {
        const QJsonObject object = entry.toObject();
        const Ban ban{object.value(u"name"_s).toString(), object.value(u"reason"_s).toString().left(maxReasonLength),
                object.value(u"by"_s).toString().left(maxReasonLength),
                QDateTime::fromString(object.value(u"since"_s).toString(), Qt::ISODate)};
        if (ban.name.isEmpty() || QcProfileValidation::playerName(ban.name) != ban.name) {
            warnings.append(u"access.json: invalid name in bans left out"_s);
        } else if (m_bans.size() >= maxEntries) {
            warnings.append(u"access.json: more than %1 bans, the rest is left out"_s.arg(maxEntries));
            return;
        } else {
            m_bans.insert(key(ban.name), ban);
        }
    }
}

QcStatus QcAccessList::write() const
{
    if (m_path.isEmpty()) {
        return {};
    }
    // The damaged file is the only copy of the old lists; it is not overwritten.
    if (m_damaged && m_damagedCopy.isEmpty()) {
        return qcError(u"%1 is damaged and could not be copied aside; it is kept until it is repaired"_s.arg(m_path));
    }

    QJsonArray bans;
    for (const Ban &ban : m_bans) {
        bans.append(QJsonObject{{u"name"_s, ban.name}, {u"reason"_s, ban.reason}, {u"by"_s, ban.by},
                                {u"since"_s, ban.since.toString(Qt::ISODate)}});
    }
    const QJsonObject root{{u"format"_s, formatVersion},
                           {u"operators"_s, namesToJson(m_operators)},
                           {u"bans"_s, bans},
                           {u"whitelist"_s, QJsonObject{{u"enabled"_s, m_whitelistEnabled}, {u"names"_s, namesToJson(m_whitelist)}}}};

    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(root).toJson()) < 0 || !file.commit()) {
        return qcError(u"Cannot write %1: %2"_s.arg(m_path, file.errorString()));
    }
    return {};
}

QcResult<bool> QcAccessList::written(bool changed) const
{
    if (!changed) {
        return false;
    }
    if (const QcStatus status = write(); !status) {
        return qcError(status.error());
    }
    return true;
}

QcResult<bool> QcAccessList::setOperator(const QString &name, bool isOperator)
{
    const QString cleaned = QcProfileValidation::playerName(name);
    if (isOperator) {
        const bool changed = !m_operators.contains(key(cleaned)) && m_operators.size() < maxEntries;
        if (changed) {
            m_operators.insert(key(cleaned), cleaned);
        }
        return written(changed);
    }
    const bool configured = m_configOperators.remove(key(cleaned)) > 0;
    const QcResult<bool> changed = written(m_operators.remove(key(cleaned)) > 0);
    return changed && configured ? QcResult<bool>(true) : changed;
}

const QcAccessList::Ban *QcAccessList::ban(const QString &name) const
{
    const auto found = m_bans.constFind(key(name));
    return found != m_bans.constEnd() ? &*found : nullptr;
}

QcResult<bool> QcAccessList::addBan(const Ban &ban)
{
    Ban cleaned = ban;
    cleaned.name = QcProfileValidation::playerName(ban.name);
    cleaned.reason = ban.reason.left(maxReasonLength);
    cleaned.by = ban.by.left(maxReasonLength);
    if (isFullFor(List::Bans, cleaned.name)) {
        return false;
    }

    // A new ban of a banned name replaces its reason.
    const Ban *existing = this->ban(cleaned.name);
    const bool changed = !existing || existing->reason != cleaned.reason || existing->by != cleaned.by;
    if (changed) {
        m_bans.insert(key(cleaned.name), cleaned);
    }
    return written(changed);
}

QcResult<bool> QcAccessList::removeBan(const QString &name)
{
    return written(m_bans.remove(key(name)) > 0);
}

QcResult<bool> QcAccessList::setWhitelistEnabled(bool enabled)
{
    const bool changed = m_whitelistEnabled != enabled;
    m_whitelistEnabled = enabled;
    return written(changed);
}

QcResult<bool> QcAccessList::setWhitelisted(const QString &name, bool listed)
{
    const QString cleaned = QcProfileValidation::playerName(name);
    if (listed) {
        const bool changed = !m_whitelist.contains(key(cleaned)) && m_whitelist.size() < maxEntries;
        if (changed) {
            m_whitelist.insert(key(cleaned), cleaned);
        }
        return written(changed);
    }
    return written(m_whitelist.remove(key(cleaned)) > 0);
}

std::optional<QString> QcAccessList::refusal(const QString &name, bool local) const
{
    if (local) {
        return std::nullopt;
    }
    if (const Ban *banned = ban(name)) {
        return banned->reason.isEmpty() ? u"You are banned from this world"_s : u"You are banned from this world: %1"_s.arg(banned->reason);
    }
    if (m_whitelistEnabled && !isOperator(name) && !isWhitelisted(name)) {
        return u"You are not on the whitelist of this world"_s;
    }
    return std::nullopt;
}
