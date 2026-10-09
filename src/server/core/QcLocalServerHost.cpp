#include "QcLocalServerHost.h"

#include "QcInProcessTransport.h"
#include "QcServer.h"
#include "QcWorldGenerator.h"

#include <QThread>

#include <mutex>

using namespace Qt::StringLiterals;

QcLocalServerHost::QcLocalServerHost(QcServerConfig config, std::shared_ptr<const QcContentRegistry> content,
                                     std::unique_ptr<QcWorldGenerator> generator, QObject *parent)
    : QObject(parent)
    , m_thread(new QThread(this))
{
    m_thread->setObjectName(u"QtCraft server"_s);
    m_server = new QcServer(std::move(config), std::move(content), std::move(generator));

    // Opened before the thread starts, so a damaged save is reported right here.
    if (const QcStatus opened = m_server->openWorld(); !opened) {
        m_startError = opened.error();
        delete m_server;
        m_server = nullptr;
        return;
    }

    m_server->moveToThread(m_thread);
    m_thread->start();
    QMetaObject::invokeMethod(m_server, &QcServer::start, Qt::QueuedConnection);
}

QcLocalServerHost::~QcLocalServerHost()
{
    if (const QcStatus stopped = shutdown(); !stopped) {
        qWarning("Saving the world failed: %s", qPrintable(stopped.error()));
    }
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

double QcLocalServerHost::tickAverageMs() const
{
    return m_server ? m_server->tickTimes().averageMs : 0.0;
}

double QcLocalServerHost::tickWorstMs() const
{
    return m_server ? m_server->tickTimes().worstMs : 0.0;
}

void QcLocalServerHost::resetTickTimes()
{
    if (m_server) {
        m_server->resetTickTimes();
    }
}

QcStatus QcLocalServerHost::save()
{
    if (!m_server) {
        return {};
    }

    // The result crosses threads under a std::mutex so ThreadSanitizer can see the hand-over
    // (decision E-010); taking the lock once first publishes the initialised value.
    std::mutex mutex;
    QcStatus result;
    { const std::scoped_lock lock(mutex); }

    QcServer *server = m_server;
    QMetaObject::invokeMethod(
        server,
        [server, &result, &mutex] {
            const std::scoped_lock lock(mutex);
            result = server->save();
        },
        Qt::BlockingQueuedConnection);

    const std::scoped_lock lock(mutex);
    return result;
}

QcStatus QcLocalServerHost::shutdown()
{
    if (!m_server) {
        return {};
    }

    QcServer *server = m_server;
    m_server = nullptr;

    std::mutex mutex;
    QcStatus result;
    { const std::scoped_lock lock(mutex); }

    QMetaObject::invokeMethod(
        server,
        [server, &result, &mutex] {
            const std::scoped_lock lock(mutex);
            result = server->stop();
            delete server;
        },
        Qt::BlockingQueuedConnection);

    m_thread->quit();
    m_thread->wait();

    const std::scoped_lock lock(mutex);
    return result;
}
