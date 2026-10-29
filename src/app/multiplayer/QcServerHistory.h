// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

class QSettings;

// The servers the player joined recently (decision E-258), newest first, kept in the settings
// file as "multiplayer/servers". An entry holds the address as typed and the name the server gave
// at the login; passwords are never stored. Two addresses for the same host and port
// ("Host", "host:25600") are one entry.
class QcServerHistory
{
public:
    static constexpr qsizetype maxEntries = 10;
    // Longer texts in a damaged or edited file are cut.
    static constexpr qsizetype maxTextLength = 256;

    struct Entry
    {
        QString address;
        QString name;
        QDateTime lastJoined;

        friend bool operator==(const Entry &, const Entry &) = default;
    };

    const QList<Entry> &entries() const { return m_entries; }
    // Puts the server first (replacing an entry for the same host and port) and drops the oldest
    // beyond maxEntries. Addresses that do not parse are ignored.
    void add(const QString &address, const QString &name, const QDateTime &when);
    // Removes the entry for this host and port; false when there is none.
    bool remove(const QString &address);

    // Unusable entries in the file (no address, unparsable) are skipped. Not const: QSettings
    // reads arrays through beginReadArray().
    static QcServerHistory load(QSettings &file);
    void save(QSettings &file) const;

    // Lower-case "host:port" for comparing addresses; empty when `address` does not parse.
    static QString key(const QString &address);

private:
    QList<Entry> m_entries;
};
