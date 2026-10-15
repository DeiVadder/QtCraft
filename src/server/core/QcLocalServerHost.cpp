#include "QcLocalServerHost.h"

#include "QcBlockEntityChecks.h"
#include "QcEntityChecks.h"
#include "QcChatText.h"
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
    m_server = std::make_unique<QcServer>(std::move(config), std::move(content), std::move(generator));

    // Opened before the thread starts, so a damaged save is reported right here.
    if (const QcStatus opened = m_server->openWorld(); !opened) {
        m_startError = opened.error();
        m_worldInUse = m_server->worldInUse();
        m_worldTooOld = m_server->worldTooOld();
        m_damagedFile = m_server->damagedFile();
        m_server.reset();
        return;
    }

    connect(m_server.get(), &QcServer::stopRequested, this, &QcLocalServerHost::stopRequested, Qt::QueuedConnection);
    m_server->moveToThread(m_thread);
    // Above the render and worker threads, so the ticks start on time (decision E-090).
    m_thread->start(QThread::HighPriority);
    QMetaObject::invokeMethod(m_server.get(), &QcServer::start, Qt::QueuedConnection);
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
        QMetaObject::invokeMethod(m_server.get(), [this, server = m_server.get()] { acceptPendingClients(server); }, Qt::QueuedConnection);
    }
    return std::move(clientEnd);
}

void QcLocalServerHost::acceptPendingClients(QcServer *server)
{
    for (std::unique_ptr<QcTransport> &transport : m_pendingClients.takeAll()) {
        // In-process transports exist only inside this process: the player who opened the world.
        server->addClient(std::move(transport), QcServer::ClientOrigin::Local);
    }
}

QcTickStats::Summary QcLocalServerHost::tickStats() const
{
    return m_server ? m_server->tickTimes() : QcTickStats::Summary();
}

QcLocalServerHost::EntityCounts QcLocalServerHost::entityCounts()
{
    if (!m_server) {
        return {};
    }

    // Counted in the server thread; the result comes back under a mutex (E-010).
    std::mutex mutex;
    EntityCounts result;
    { const std::scoped_lock lock(mutex); }
    QcServer *server = m_server.get();
    QMetaObject::invokeMethod(
        server,
        [server, &result, &mutex] {
            const std::scoped_lock lock(mutex);
            const QcBlockEntities &entities = server->blockEntities();
            result = {server->mobs().mobs().size(), server->items().items().size(),
                      entities.containerCount() + entities.machineCount() + entities.saplingCount(),
                      QcBlockEntityChecks::problems(server->world()).size(), QcEntityChecks::problems(server->entities()).size()};
        },
        Qt::BlockingQueuedConnection);

    const std::scoped_lock lock(mutex);
    return result;
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

    QcServer *server = m_server.get();
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

QcResult<quint16> QcLocalServerHost::listen(const QcNetworkConfig &config, const std::optional<QcLoginSettings> &login)
{
    if (!m_server) {
        return qcError(u"No world is open"_s);
    }

    // Made and used only in the server thread; the result comes back under a mutex (E-010).
    std::mutex mutex;
    QcResult<quint16> result = qcError(QString());
    { const std::scoped_lock lock(mutex); }
    QcServer *server = m_server.get();
    QMetaObject::invokeMethod(
        server,
        [this, server, &config, &login, &result, &mutex] {
            const std::scoped_lock lock(mutex);
            if (login) {
                server->setLoginSettings(*login);
            }
            // The old listener closes its port before the new one opens.
            m_network.reset();
            m_network = std::make_unique<QcNetworkServer>(*server, config);
            result = m_network->listen();
            if (!result) {
                m_network.reset();
            }
        },
        Qt::BlockingQueuedConnection);

    const std::scoped_lock lock(mutex);
    m_listeningPort = result ? *result : 0;
    return result;
}

void QcLocalServerHost::stopListening()
{
    if (!m_server) {
        return;
    }

    QMetaObject::invokeMethod(
        m_server.get(),
        [this] {
            m_server->disconnectRemoteClients();
            m_network.reset();
        },
        Qt::BlockingQueuedConnection);
    m_listeningPort = 0;
}

QStringList QcLocalServerHost::runConsoleCommand(const QString &line)
{
    if (!m_server) {
        return {u"No world is open"_s};
    }

    // The replies come back under a std::mutex like the result of save() (decision E-010).
    std::mutex mutex;
    QStringList replies;
    // Publishes the empty list to the server thread (ThreadSanitizer does not see Qt's handover).
    { const std::scoped_lock lock(mutex); }
    QcServer *server = m_server.get();
    QMetaObject::invokeMethod(
        server,
        [server, &line, &replies, &mutex] {
            const std::scoped_lock lock(mutex);
            server->runConsoleCommand(line, [&replies](const QcMsg::ChatMessage &reply) { replies.append(QcChatText::display(reply)); });
        },
        Qt::BlockingQueuedConnection);

    const std::scoped_lock lock(mutex);
    return replies;
}

QcStatus QcLocalServerHost::shutdown()
{
    if (!m_server) {
        return {};
    }

    std::unique_ptr<QcServer> server = std::move(m_server);
    QcServer *context = server.get();

    // Taking the lock once publishes `server` and `result` to the server thread, which resets the
    // pointer; this thread touches `server` again only after its final lock below.
    std::mutex mutex;
    QcStatus result;
    { const std::scoped_lock lock(mutex); }

    QMetaObject::invokeMethod(
        context,
        [this, &server, &result, &mutex] {
            const std::scoped_lock lock(mutex);
            result = server->stop();
            // Both die in the server thread they live in, the listener first.
            m_network.reset();
            server.reset();
        },
        Qt::BlockingQueuedConnection);

    m_thread->quit();
    m_thread->wait();
    m_listeningPort = 0;

    const std::scoped_lock lock(mutex);
    return result;
}
