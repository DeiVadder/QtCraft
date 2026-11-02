// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcResult.h"
#include "network/QcServerAddress.h"

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QString>

#include <optional>

/*! \brief The servers this installation trusts (decision E-872), like SSH's known_hosts: the
    fingerprint of each server's certificate (QcTlsFingerprint), pinned at the first connection
    (trust on first use) under the normalised address (keyOf(): every spelling of one address is one
    pin).

    A server that later shows another certificate is refused until the player forgets the pin.
    Kept as JSON in <data>/known-servers.json, readable only by the user; the file is read anew
    for every join. */
class QcKnownServers
{
public:
    /*! How a server's certificate compares with its pin. */
    enum class Trust {
        /*! No pin for this address yet. */
        New,
        /*! The pinned certificate. */
        Known,
        /*! Another certificate than the pinned one. */
        Changed,
    };

    /*! \brief A pin. */
    struct Entry
    {
        /*! The certificate's fingerprint (QcTlsFingerprint). */
        QByteArray fingerprint;
        /*! When it was pinned. */
        QDateTime pinned;
    };

    /*! The file's name in the data root. */
    static constexpr char fileName[] = "known-servers.json";

    /*! Returns the file's place: <data root>/known-servers.json (QcDataRoot). */
    [[nodiscard]] static QString defaultPath();
    /*! Returns the key of \a address, "host:port": a name in lower case, an IP address in Qt's
        canonical form (IPv6 in brackets, "[2a02:db8::1]" for "2A02:0DB8:0::1"). */
    [[nodiscard]] static QString keyOf(const QcServerAddress &address);

    explicit QcKnownServers(QString path);

    /*! Reads the file. Returns an error for a damaged file; a missing file is an empty list (it
        is not overwritten: the pins would be lost). */
    [[nodiscard]] QcStatus load();
    /*! Returns how the certificate with \a fingerprint compares with the pin of \a address. */
    [[nodiscard]] Trust check(const QcServerAddress &address, const QByteArray &fingerprint) const;
    /*! Returns the pin of \a address; nothing when there is none. */
    [[nodiscard]] std::optional<Entry> find(const QcServerAddress &address) const;
    /*! Pins \a fingerprint for \a address and writes the file. Returns the error when the file
        cannot be written. */
    [[nodiscard]] QcStatus remember(const QcServerAddress &address, const QByteArray &fingerprint);
    /*! Drops the pin of \a address and writes the file. Returns the error when the file cannot
        be written; nothing to drop is no error. */
    [[nodiscard]] QcStatus forget(const QcServerAddress &address);

private:
    [[nodiscard]] QcStatus save() const;

    QString m_path;
    QHash<QString, Entry> m_entries;
};
