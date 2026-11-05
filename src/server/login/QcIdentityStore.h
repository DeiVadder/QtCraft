// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcResult.h"

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QString>
#include <QStringList>

/*! \brief The player identities a world has seen (decision E-252), kept in identities.json in the
    world directory: the server's salt and, per player name, the identity its first login registered
    (trust on first use).

    Later logins of that name must prove the same identity. Names compare like the access list's
    (cleaned, without case). Without a world directory everything lives in memory. A damaged file
    is copied aside and the world starts with a new salt and no identities (every fingerprint
    changes, so access entries bound to the old ones match nobody: fail closed). */
class QcIdentityStore
{
public:
    /*! The name of the file in the world directory. */
    static constexpr QStringView fileName = u"identities.json";
    /*! The format of the file. */
    static constexpr int formatVersion = 1;
    /*! Identities kept at most. */
    static constexpr qsizetype maxEntries = 65536;
    /*! The largest file read. */
    static constexpr qint64 maxFileBytes = qint64{16} * 1024 * 1024;

    /*! \brief One registered identity: the player name, the identity
        (QcLoginCrypto::keyBytes bytes), its fingerprint and when it was registered. \qcfields */
    struct Identity
    {
        QString name;
        QByteArray identity;
        QString fingerprint;
        QDateTime since;
    };

    /*! A store in memory with a random salt. */
    QcIdentityStore();

    /*! Reads identities.json in \a directory. Returns the warnings that name its problems. A new
        store keeps its random salt, written with the first identity. */
    [[nodiscard]] QStringList open(const QString &directory);

    /*! Returns the server's salt of the fingerprints. */
    const QByteArray &salt() const { return m_salt; }
    /*! Returns the identity registered for \a name; nullptr when there is none. */
    const Identity *find(const QString &name) const;
    /*! Returns the fingerprint registered for \a name, empty when there is none. */
    QString fingerprintOf(const QString &name) const;
    /*! Registers \a identity as the first one of \a name at \a now. Returns \c false when the
        store is full, the name is taken or the identity has the wrong size. It counts at once
        and is written by the next flush() (decision E-273). */
    bool add(const QString &name, const QByteArray &identity, const QDateTime &now);
    /*! Returns the number of identities. */
    qsizetype size() const { return m_identities.size(); }
    /*! Returns whether maxEntries identities are registered. */
    bool isFull() const { return m_identities.size() >= maxEntries; }
    /*! Writes the file when identities were added since the last flush. Returns an error when it
        could not be written (the identities still count until the restart, the next addition tries
        again). */
    [[nodiscard]] QcStatus flush();
    /*! Returns whether identities were added since the last flush. */
    bool hasUnsavedChanges() const { return m_dirty; }

private:
    [[nodiscard]] QStringList read(const QString &path);
    [[nodiscard]] QcStatus write() const;

    QString m_path;
    QByteArray m_salt;
    QHash<QString, Identity> m_identities;
    // The damaged file could not be copied aside: it is not overwritten.
    bool m_readOnly = false;
    bool m_dirty = false;
};
