#include "common/protocol/QcPackComparison.h"

#include <QHash>

using namespace Qt::StringLiterals;

namespace QcPackComparison {

QStringList differences(const QList<QcPackFingerprint> &server, const QList<QcPackFingerprint> &client)
{
    QHash<QString, QcPackFingerprint> clientPacks;
    for (const QcPackFingerprint &pack : client) {
        clientPacks.insert(pack.id, pack);
    }

    QStringList lines;
    for (const QcPackFingerprint &pack : server) {
        const auto found = clientPacks.constFind(pack.id);
        if (found == clientPacks.constEnd()) {
            lines.append(u"missing: %1 %2"_s.arg(pack.id, pack.version));
        } else if (found->version != pack.version) {
            lines.append(u"%1: server %2, client %3"_s.arg(pack.id, pack.version, found->version));
        } else if (!pack.hash.isEmpty() && found->hash != pack.hash) {
            // A client without a hash for a pack the server hashed is refused like other files:
            // otherwise sending no hash would skip the comparison.
            lines.append(u"%1 %2: different files"_s.arg(pack.id, pack.version));
        }
        clientPacks.remove(pack.id);
    }

    // What only the client has, in the client's order.
    for (const QcPackFingerprint &pack : client) {
        if (clientPacks.contains(pack.id)) {
            lines.append(u"extra: %1 %2"_s.arg(pack.id, pack.version));
            clientPacks.remove(pack.id);
        }
    }
    return lines;
}

QString summary(const QStringList &differences)
{
    QString text = differences.first(std::min(maxNamed, differences.size())).join(u"; "_s);
    if (differences.size() > maxNamed) {
        text += u"; and %1 more"_s.arg(differences.size() - maxNamed);
    }
    return text;
}

} // namespace QcPackComparison
