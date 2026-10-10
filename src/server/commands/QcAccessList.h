#pragma once

#include "QcResult.h"

#include <QDateTime>
#include <QJsonValue>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <optional>

// Who may join a world and who is an operator (decision E-244): operators, banned names and the
// whitelist, kept in access.json in the world directory and written at once with every change.
// Names are cleaned like player names (QcProfileValidation) and compared without case. A damaged
// file is copied aside and the world opens with empty lists but a switched-on whitelist, so nobody
// banned slips in until an operator has looked at it; when no copy could be made, nothing is
// written over the damaged file. Without a world directory the lists live in memory only.
class QcAccessList
{
public:
    static constexpr QStringView fileName = u"access.json";
    static constexpr int formatVersion = 1;
    // Per list; further entries of a file are left out with a warning.
    static constexpr qsizetype maxEntries = 4096;
    static constexpr qint64 maxFileBytes = qint64(1024) * 1024;
    static constexpr qsizetype maxReasonLength = 128;

    enum class List { Operators, Bans, Whitelist };

    struct Ban
    {
        QString name;
        QString reason;
        QString by;
        QDateTime since;
    };

    // Reads <directory>/access.json (an empty directory keeps the lists in memory). Never fails:
    // problems end up in `warnings`. `initialOperators` (server configuration) are operators too.
    void open(const QString &directory, const QStringList &initialOperators, QStringList &warnings);

    // The comparison key of a name: cleaned and case-folded.
    static QString key(const QString &name);
    // Whether adding `name` to the list would go beyond maxEntries (a listed name always fits).
    bool isFullFor(List list, const QString &name) const;
    // The file was damaged when the world opened; the copy of it (empty when none could be made,
    // then changes are not written).
    bool wasDamaged() const { return m_damaged; }
    const QString &damagedCopy() const { return m_damagedCopy; }

    bool isOperator(const QString &name) const { return m_operators.contains(key(name)) || m_configOperators.contains(key(name)); }
    QStringList operators() const;
    // Each returns whether the list changed; an error when it changed but could not be written.
    QcResult<bool> setOperator(const QString &name, bool isOperator);

    const Ban *ban(const QString &name) const;
    QList<Ban> bans() const { return m_bans.values(); }
    // Bans a name, or gives a banned name the new reason.
    QcResult<bool> addBan(const Ban &ban);
    QcResult<bool> removeBan(const QString &name);

    bool whitelistEnabled() const { return m_whitelistEnabled; }
    bool isWhitelisted(const QString &name) const { return m_whitelist.contains(key(name)); }
    QStringList whitelist() const { return m_whitelist.values(); }
    QcResult<bool> setWhitelistEnabled(bool enabled);
    QcResult<bool> setWhitelisted(const QString &name, bool listed);

    // Why a player may not join: banned (operators too), or not on the switched-on whitelist
    // (operators pass it); the local player always may. std::nullopt when he may.
    std::optional<QString> refusal(const QString &name, bool local) const;

private:
    QcStatus write() const;
    QcResult<bool> written(bool changed) const;
    void read(const QString &path, QStringList &warnings);
    void openDamaged(const QString &path);
    static void readNames(const QJsonValue &value, QMap<QString, QString> &names, const QString &list, QStringList &warnings);
    void readBans(const QJsonValue &value, QStringList &warnings);

    QString m_path;
    // Key -> name as given.
    QMap<QString, QString> m_operators;
    // Operators of the server configuration: not written to the file, /deop holds until a restart.
    QMap<QString, QString> m_configOperators;
    QMap<QString, QString> m_whitelist;
    QMap<QString, Ban> m_bans;
    bool m_whitelistEnabled = false;
    bool m_damaged = false;
    QString m_damagedCopy;
};
