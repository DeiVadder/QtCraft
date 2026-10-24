#include "headless/QcHeadlessJoin.h"

#include "clientcore/network/QcKnownServers.h"
#include "clientcore/network/QcNetworkClient.h"
#include "clientcore/session/QcClientSession.h"
#include "common/auth/QcPlayerKey.h"
#include "network/QcServerAddress.h"
#include "worlds/QcContentFingerprint.h"

#include <QDir>

using namespace Qt::StringLiterals;

QcHeadlessJoin::QcHeadlessJoin(QObject *parent)
    : QObject(parent)
    , m_client(new QcNetworkClient(this))
{
    connect(m_client, &QcNetworkClient::joined, this, &QcHeadlessJoin::joined);
    connect(m_client, &QcNetworkClient::failed, this, [this](const QString &reason) { emit failed(reason); });
}

std::unique_ptr<QcClientSession> QcHeadlessJoin::takeSession()
{
    return m_client->takeSession();
}

QcStatus QcHeadlessJoin::start(const Options &options, const QcModLoader::Result &content)
{
    const QcResult<QcServerAddress> address = QcServerAddress::parse(options.address);
    if (!address) {
        return qcError(u"Invalid address '%1': %2"_s.arg(options.address, address.error()));
    }

    const QcResult<QByteArray> key = QcPlayerKey::loadOrCreate(QDir(options.dataDirectory).filePath(QString::fromLatin1(QcPlayerKey::fileName)));
    if (!key) {
        return qcError(key.error());
    }

    QcNetworkClient::Request request;
    request.address = *address;
    request.playerName = options.playerName;
    request.tickMode = QcSessionTickMode::Timer;
    // Each bot pins the servers in its own data folder.
    request.knownServersFile = QDir(options.dataDirectory).filePath(QString::fromLatin1(QcKnownServers::fileName));
    request.credentials = {*key, options.password, QcContentFingerprint::of(*content.content, content.mods), options.viewDistance};
    m_client->join(request);
    return {};
}
