#pragma once

#include "QcResult.h"
#include "QcSessionMessages.h"

#include <QDateTime>
#include <QJsonValue>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <optional>

// Who may join a world and who is an operator (decision E-244): operators, banned names and the
// whitelist, kept in access.json in the world directory and written at once with every change.
// Names are cleaned like player names (QcProfileValidation) and compared without case. Entries are
// bound to the fingerprint of a player's identity (decision E-253): an entry with a fingerprint
// counts only for that identity, a ban with one also under other names; an entry without one
// counts by name and takes the fingerprint of the first player of that name who logs in. A damaged
// file is copied aside and the world opens with empty lists but a switched-on whitelist, so nobody
// banned slips in until an operator has looked at it; when no copy could be made, nothing is
// written over the damaged file. Without a world directory the lists live in memory only.
class QcAccessList
{
public:
    static constexpr QStringView fileName = u"access.json";
    // Format 2 has the fingerprints; files of other formats count as damaged (decision E-356: no
    // migration before the first release).
    static constexpr int formatVersion = 2;
    // Per list; further entries of a file are left out with a warning.
    static constexpr qsizetype maxEntries = 4096;
    static constexpr qint64 maxFileBytes = qint64(1024) * 1024;
    static constexpr qsizetype maxReasonLength = 128;

    enum class List { Operators, Bans, Whitelist };

    struct Entry
    {
        QString name;
        // QcLoginCrypto::fingerprint of the player's identity; empty until known.
        QString fingerprint;
    };

    struct Ban
    {
        QString name;
        QString reason;
        QString by;
        QDateTime since;
        QString fingerprint;
    };

    // Reads <directory>/access.json (an empty directory keeps the lists in memory). Never fails:
    // problems end up in `warnings`. `initialOperators` (server configuration) are operators too;
    // invalid names among them are left out with a warning.
    void open(const QString &directory, const QStringList &initialOperators, QStringList &warnings);

    // The comparison key of a name: cleaned and case-folded.
    static QString key(const QString &name);
    // Whether adding `name` to the list would go beyond maxEntries (a listed name always fits).
    bool isFullFor(List list, const QString &name) const;
    // The file was damaged when the world opened; the copy of it (empty when none could be made,
    // then changes are not written).
    bool wasDamaged() const { return m_damaged; }
    const QString &damagedCopy() const { return m_damagedCopy; }

    // Operators of the server configuration count for the identity of their first login, which
    // bind() keeps in the file (configOperators), and by name until then.
    bool isOperator(const QString &name, const QString &fingerprint) const;
    QStringList operators() const;
    // Each returns whether the list changed; an error when it changed but could not be written.
    // A known fingerprint binds the entry to that identity.
    [[nodiscard]] QcResult<bool> setOperator(const QString &name, bool isOperator, const QString &fingerprint = {});

    const Ban *ban(const QString &name) const;
    QList<Ban> bans() const { return m_bans.values(); }
    // Bans a name, or gives a banned name the new reason.
    [[nodiscard]] QcResult<bool> addBan(const Ban &ban);
    [[nodiscard]] QcResult<bool> removeBan(const QString &name);

    bool whitelistEnabled() const { return m_whitelistEnabled; }
    bool isWhitelisted(const QString &name, const QString &fingerprint) const;
    QStringList whitelist() const;
    [[nodiscard]] QcResult<bool> setWhitelistEnabled(bool enabled);
    [[nodiscard]] QcResult<bool> setWhitelisted(const QString &name, bool listed, const QString &fingerprint = {});

    // A player logged in with this identity: entries of his name without a fingerprint get it.
    [[nodiscard]] QcResult<bool> bind(const QString &name, const QString &fingerprint);

    // Why a player may not join: banned by name or fingerprint (operators too), or not on the
    // switched-on whitelist (operators pass it); the local player always may. The reason is a
    // translatable disconnect (QcChatText); std::nullopt when he may.
    [[nodiscard]] std::optional<QcMsg::Disconnect> refusal(const QString &name, const QString &fingerprint, bool local) const;

private:
    [[nodiscard]] QcStatus write() const;
    [[nodiscard]] QcResult<bool> written(bool changed) const;
    void read(const QString &path, QStringList &warnings);
    void openDamaged(const QString &path);
    static void readNames(const QJsonValue &value, QMap<QString, Entry> &names, const QString &list, QStringList &warnings);
    // An entry with this fingerprint, or one without any, matches.
    static bool matches(const QString &entryFingerprint, const QString &fingerprint);
    const Ban *banOf(const QString &name, const QString &fingerprint) const;
    // Adds the entry, or binds a listed one without a fingerprint to the entry's; whether it changed.
    static bool insertEntry(QMap<QString, Entry> &entries, const Entry &entry);
    void readBans(const QJsonValue &value, QStringList &warnings);

    QString m_path;
    // Key -> name as given with its fingerprint.
    QMap<QString, Entry> m_operators;
    // Operators of the server configuration: not written to the file, /deop holds until a restart.
    QMap<QString, QString> m_configOperators;
    // Key -> the identity an operator of the configuration first logged in with (written; kept
    // while the name is left out of the configuration).
    QMap<QString, Entry> m_configBindings;
    QMap<QString, Entry> m_whitelist;
    QMap<QString, Ban> m_bans;
    bool m_whitelistEnabled = false;
    bool m_damaged = false;
    QString m_damagedCopy;
};
