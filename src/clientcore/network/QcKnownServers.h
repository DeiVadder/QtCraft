#pragma once

#include "common/core/QcResult.h"
#include "network/QcServerAddress.h"

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QString>

#include <optional>

// The servers this installation trusts (decision E-872), like SSH's known_hosts: the fingerprint
// of each server's certificate (QcTlsFingerprint), pinned at the first connection (trust on first
// use) under the normalised address (keyOf(): every spelling of one address is one pin). A server
// that later shows another certificate is refused until the player forgets the pin. Kept as JSON
// in <data>/known-servers.json, readable only by the user; the file is read anew for every join.
class QcKnownServers
{
public:
    enum class Trust {
        // No pin for this address yet.
        New,
        // The pinned certificate.
        Known,
        // Another certificate than the pinned one.
        Changed,
    };

    struct Entry
    {
        QByteArray fingerprint;
        QDateTime pinned;
    };

    static constexpr char fileName[] = "known-servers.json";

    // <data root>/known-servers.json (QcDataRoot).
    [[nodiscard]] static QString defaultPath();
    // "host:port": a name in lower case, an IP address in Qt's canonical form (IPv6 in brackets,
    // "[2a02:db8::1]" for "2A02:0DB8:0::1").
    [[nodiscard]] static QString keyOf(const QcServerAddress &address);

    explicit QcKnownServers(QString path);

    // Reads the file; a missing file is an empty list, a damaged one an error (it is not
    // overwritten: the pins would be lost).
    [[nodiscard]] QcStatus load();
    [[nodiscard]] Trust check(const QcServerAddress &address, const QByteArray &fingerprint) const;
    [[nodiscard]] std::optional<Entry> find(const QcServerAddress &address) const;
    // Pins `fingerprint` for the address and writes the file.
    [[nodiscard]] QcStatus remember(const QcServerAddress &address, const QByteArray &fingerprint);
    // Drops the pin of the address and writes the file; nothing to drop is no error.
    [[nodiscard]] QcStatus forget(const QcServerAddress &address);

private:
    [[nodiscard]] QcStatus save() const;

    QString m_path;
    QHash<QString, Entry> m_entries;
};
