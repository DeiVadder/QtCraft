#include "QcLocalServerHost.h"

#include "QcInProcessTransport.h"
#include "QcServer.h"
#include "QcWorldGenerator.h"

#include <QThread>

using namespace Qt::StringLiterals;

QcLocalServerHost::QcLocalServerHost(QcServerConfig config, std::shared_ptr<const QcContentRegistry> content,
                                     std::unique_ptr<QcWorldGenerator> generator, QObject *parent)
    : QObject(parent)
    , m_thread(new QThread(this))
{
    m_thread->setObjectName(u"QtCraft server"_s);
    m_server = new QcServer(std::move(config), std::move(content), std::move(generator));
    m_server->moveToThread(m_thread);
    m_thread->start();
    QMetaObject::invokeMethod(m_server, &QcServer::start, Qt::QueuedConnection);
}

QcLocalServerHost::~QcLocalServerHost()
{
    shutdown();
}

std::unique_ptr<QcTransport> QcLocalServerHost::connectClient()
{
    auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
    if (!m_server) {
        clientEnd->close();
        return std::move(clientEnd);
    }
    serverEnd->moveToThread(m_thread);
    if (m_pendingClients.push(std::move(serverEnd))) {
        QMetaObject::invokeMethod(m_server, [this, server = m_server] { acceptPendingClients(server); }, Qt::QueuedConnection);
    }
    return std::move(clientEnd);
}

void QcLocalServerHost::acceptPendingClients(QcServer *server)
{
    for (std::unique_ptr<QcTransport> &transport : m_pendingClients.takeAll()) {
        server->addClient(std::move(transport));
    }
}

void QcLocalServerHost::shutdown()
{
    if (!m_server) {
        return;
    }
    QcServer *server = m_server;
    m_server = nullptr;
    QMetaObject::invokeMethod(
        server,
        [server] {
            server->stop();
            delete server;
        },
        Qt::BlockingQueuedConnection);
    m_thread->quit();
    m_thread->wait();
}
