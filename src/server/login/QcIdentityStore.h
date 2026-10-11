#pragma once

#include "QcResult.h"

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QString>
#include <QStringList>

// The player identities a world has seen (decision E-252), kept in identities.json in the world
// directory: the server's salt and, per player name, the identity its first login registered
// (trust on first use). Later logins of that name must prove the same identity. Names compare like
// the access list's (cleaned, without case). Without a world directory everything lives in memory.
// A damaged file is copied aside and the world starts with a new salt and no identities (every
// fingerprint changes, so access entries bound to the old ones match nobody: fail closed).
class QcIdentityStore
{
public:
    static constexpr QStringView fileName = u"identities.json";
    static constexpr int formatVersion = 1;
    static constexpr qsizetype maxEntries = 65536;
    static constexpr qint64 maxFileBytes = qint64(16) * 1024 * 1024;

    struct Identity
    {
        QString name;
        QByteArray identity;
        QString fingerprint;
        QDateTime since;
    };

    // A store in memory with a random salt.
    QcIdentityStore();

    // Reads <directory>/identities.json; problems end up in `warnings`. A new store keeps its random
    // salt, written with the first identity.
    void open(const QString &directory, QStringList &warnings);

    const QByteArray &salt() const { return m_salt; }
    const Identity *find(const QString &name) const;
    // The fingerprint registered for a name, empty when there is none.
    QString fingerprintOf(const QString &name) const;
    // Registers the first identity of a name; false when the store is full or the name is taken.
    // It counts at once and is written by the next flush() (decision E-273).
    bool add(const QString &name, const QByteArray &identity, const QDateTime &now);
    qsizetype size() const { return m_identities.size(); }
    bool isFull() const { return m_identities.size() >= maxEntries; }
    // Writes the file when identities were added since the last flush; an error when it could not
    // be written (the identities still count until the restart, the next addition tries again).
    QcStatus flush();
    bool hasUnsavedChanges() const { return m_dirty; }

private:
    void read(const QString &path, QStringList &warnings);
    QcStatus write() const;

    QString m_path;
    QByteArray m_salt;
    QHash<QString, Identity> m_identities;
    // The damaged file could not be copied aside: it is not overwritten.
    bool m_readOnly = false;
    bool m_dirty = false;
};
