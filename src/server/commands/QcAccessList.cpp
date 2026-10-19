#include "server/commands/QcAccessList.h"

#include "common/protocol/QcChatText.h"
#include "server/core/QcProfileValidation.h"
#include "server/core/QcServerLogging.h"
#include "server/storage/QcSaveFile.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

QJsonArray entriesToJson(const QMap<QString, QcAccessList::Entry> &entries)
{
    QJsonArray array;
    for (const QcAccessList::Entry &entry : entries) {
        array.append(QJsonObject{{u"name"_s, entry.name}, {u"fingerprint"_s, entry.fingerprint}});
    }
    return array;
}

// A fingerprint as QcLoginCrypto makes it, or empty.
bool validFingerprint(const QString &fingerprint)
{
    static const QRegularExpression hex(u"^[0-9a-f]{32}$"_s);
    return fingerprint.isEmpty() || hex.matchView(fingerprint).hasMatch();
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
        if (const std::optional<QString> valid = QcProfileValidation::validPlayerName(name)) {
            m_configOperators.insert(key(*valid), *valid);
        } else {
            warnings.append(u"Invalid operator name '%1' in the server configuration left out"_s.arg(name));
        }
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

bool QcAccessList::matches(const QString &entryFingerprint, const QString &fingerprint)
{
    return entryFingerprint.isEmpty() || entryFingerprint == fingerprint;
}

bool QcAccessList::isOperator(const QString &name, const QString &fingerprint) const
{
    const QString nameKey = key(name);
    const auto found = m_operators.constFind(nameKey);
    if (found != m_operators.constEnd() && matches(found->fingerprint, fingerprint)) {
        return true;
    }
    // An operator of the configuration counts for the identity his first login bound.
    return m_configOperators.contains(nameKey) && matches(m_configBindings.value(nameKey).fingerprint, fingerprint);
}

QStringList QcAccessList::operators() const
{
    QMap<QString, QString> all = m_configOperators;
    for (auto it = m_operators.constBegin(); it != m_operators.constEnd(); ++it) {
        all.insert(it.key(), it->name);
    }
    return all.values();
}

bool QcAccessList::isWhitelisted(const QString &name, const QString &fingerprint) const
{
    const auto found = m_whitelist.constFind(key(name));
    return found != m_whitelist.constEnd() && matches(found->fingerprint, fingerprint);
}

QStringList QcAccessList::whitelist() const
{
    QStringList names;
    for (const Entry &entry : m_whitelist) {
        names.append(entry.name);
    }
    return names;
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
    readNames(root.value(u"configOperators"_s), m_configBindings, u"configOperators"_s, warnings);
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
    m_damagedCopy = QcSaveFile::keepAside(path);
    m_whitelistEnabled = true;
    if (m_damagedCopy.isEmpty()) {
        qCWarning(lcServer).noquote() << u"%1 is damaged and could not be copied aside: operators, bans and the whitelist start "
                                         u"empty with the whitelist on, and changes are not saved until the file is repaired"_s.arg(path);
    } else {
        qCWarning(lcServer).noquote() << u"%1 is damaged: operators, bans and the whitelist start empty with the whitelist on; "
                                         u"the damaged file was kept as %2"_s.arg(path, m_damagedCopy);
    }
}

void QcAccessList::readNames(const QJsonValue &value, QMap<QString, Entry> &names, const QString &list, QStringList &warnings)
{
    const QJsonArray array = value.toArray();
    for (const auto &item : array) {
        const QJsonObject object = item.toObject();
        const Entry entry{object.value(u"name"_s).toString(), object.value(u"fingerprint"_s).toString()};
        if (entry.name.isEmpty() || QcProfileValidation::playerName(entry.name) != entry.name || !validFingerprint(entry.fingerprint)) {
            warnings.append(u"access.json: invalid name in %1 left out"_s.arg(list));
        } else if (names.size() >= maxEntries) {
            warnings.append(u"access.json: more than %1 entries in %2, the rest is left out"_s.arg(maxEntries).arg(list));
            return;
        } else {
            names.insert(key(entry.name), entry);
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
                QDateTime::fromString(object.value(u"since"_s).toString(), Qt::ISODate), object.value(u"fingerprint"_s).toString()};
        if (ban.name.isEmpty() || QcProfileValidation::playerName(ban.name) != ban.name || !validFingerprint(ban.fingerprint)) {
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
                                {u"since"_s, ban.since.toString(Qt::ISODate)}, {u"fingerprint"_s, ban.fingerprint}});
    }
    const QJsonObject root{{u"format"_s, formatVersion},
                           {u"operators"_s, entriesToJson(m_operators)},
                           {u"configOperators"_s, entriesToJson(m_configBindings)},
                           {u"bans"_s, bans},
                           {u"whitelist"_s, QJsonObject{{u"enabled"_s, m_whitelistEnabled}, {u"names"_s, entriesToJson(m_whitelist)}}}};

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

QcResult<bool> QcAccessList::setOperator(const QString &name, bool isOperator, const QString &fingerprint)
{
    const QString cleaned = QcProfileValidation::playerName(name);
    if (isOperator) {
        return written(insertEntry(m_operators, {cleaned, fingerprint}));
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

    // A new ban of a banned name replaces its reason and keeps a known fingerprint.
    const Ban *existing = this->ban(cleaned.name);
    if (existing && cleaned.fingerprint.isEmpty()) {
        cleaned.fingerprint = existing->fingerprint;
    }
    const bool changed = !existing || existing->reason != cleaned.reason || existing->by != cleaned.by
                         || existing->fingerprint != cleaned.fingerprint;
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

QcResult<bool> QcAccessList::setWhitelisted(const QString &name, bool listed, const QString &fingerprint)
{
    const QString cleaned = QcProfileValidation::playerName(name);
    if (listed) {
        return written(insertEntry(m_whitelist, {cleaned, fingerprint}));
    }
    return written(m_whitelist.remove(key(cleaned)) > 0);
}

bool QcAccessList::insertEntry(QMap<QString, Entry> &entries, const Entry &entry)
{
    // A listed name keeps its entry; a newly known fingerprint binds it.
    const auto found = entries.find(key(entry.name));
    if (found != entries.end()) {
        const bool binds = found->fingerprint.isEmpty() && !entry.fingerprint.isEmpty();
        if (binds) {
            found->fingerprint = entry.fingerprint;
        }
        return binds;
    }

    if (entries.size() >= maxEntries) {
        return false;
    }
    entries.insert(key(entry.name), entry);
    return true;
}

QcResult<bool> QcAccessList::bind(const QString &name, const QString &fingerprint)
{
    if (fingerprint.isEmpty()) {
        return false;
    }

    bool changed = false;
    const QString nameKey = key(name);
    for (QMap<QString, Entry> *entries : {&m_operators, &m_whitelist}) {
        const auto found = entries->find(nameKey);
        if (found != entries->end() && found->fingerprint.isEmpty()) {
            found->fingerprint = fingerprint;
            changed = true;
        }
    }

    // An operator of the configuration keeps the identity of his first login across restarts.
    const auto configured = m_configOperators.constFind(nameKey);
    if (configured != m_configOperators.constEnd() && m_configBindings.value(nameKey).fingerprint.isEmpty()
        && m_configBindings.size() < maxEntries) {
        m_configBindings.insert(nameKey, {*configured, fingerprint});
        changed = true;
    }

    const auto ban = m_bans.find(nameKey);
    if (ban != m_bans.end() && ban->fingerprint.isEmpty()) {
        ban->fingerprint = fingerprint;
        changed = true;
    }
    return written(changed);
}

const QcAccessList::Ban *QcAccessList::banOf(const QString &name, const QString &fingerprint) const
{
    // A name ban holds whatever the identity; a ban with a fingerprint also under other names.
    if (const Ban *byName = ban(name)) {
        return byName;
    }
    if (fingerprint.isEmpty()) {
        return nullptr;
    }

    const auto found = std::ranges::find(m_bans, fingerprint, &Ban::fingerprint);
    return found != m_bans.end() ? &*found : nullptr;
}

std::optional<QcMsg::Disconnect> QcAccessList::refusal(const QString &name, const QString &fingerprint, bool local) const
{
    if (local) {
        return std::nullopt;
    }
    if (const Ban *banned = banOf(name, fingerprint)) {
        return banned->reason.isEmpty()
                   ? QcChatText::disconnect(QT_TRANSLATE_NOOP("QcServerMessages", "You are banned from this world"))
                   : QcChatText::disconnect(QT_TRANSLATE_NOOP("QcServerMessages", "You are banned from this world: %1"), {banned->reason});
    }
    if (m_whitelistEnabled && !isOperator(name, fingerprint) && !isWhitelisted(name, fingerprint)) {
        return QcChatText::disconnect(QT_TRANSLATE_NOOP("QcServerMessages", "You are not on the whitelist of this world"));
    }
    return std::nullopt;
}
