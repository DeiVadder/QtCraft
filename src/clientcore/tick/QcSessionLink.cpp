#include "clientcore/tick/QcSessionLink.h"

#include "clientcore/tick/QcSessionCore.h"
#include "common/protocol/QcTransport.h"

#include <QPointer>
#include <QThread>

#include <mutex>

using namespace Qt::StringLiterals;

QcSessionLink::QcSessionLink(std::unique_ptr<QcTransport> transport, Handler handler)
    : m_handler(std::move(handler))
    , m_core(std::make_unique<QcSessionCore>(std::move(transport), [this](QcSessionEvent &&event) { deliver(std::move(event)); }))
{
}

QcSessionLink::~QcSessionLink()
{
    if (!m_thread) {
        return;
    }

    // Queued after the wake-ups of the commands given before, so those still reach the core. The
    // core stops and comes back with its connection; nothing calls deliver() any more. The
    // connection closes here: a TCP socket lingers in this thread (QcTcpTransport::lingerAndDelete),
    // which goes on, rather than in the tick thread, which ends now.
    //
    // The core thread hands the core back through a std::mutex and std::condition_variable rather
    // than a Qt::BlockingQueuedConnection: the latter waits on a semaphore inside QtCore, which TSan
    // does not instrument, so TSan would see no ordering between the core's last steps in its thread
    // and what this thread does with the core afterwards. For the same reason the queued call carries
    // only `this` (as the wake-ups do, tests/tsan.supp); the core thread signals while holding the
    // mutex, its last access to the link.
    QMetaObject::invokeMethod(m_core.get(), [this] { handBack(); }, Qt::QueuedConnection);
    {
        std::unique_lock lock(m_handBackMutex);
        m_handedBackChanged.wait(lock, [this] { return m_handedBack; });
    }

    m_core->shutdown();
    m_thread->quit();
    m_thread->wait();
}

bool QcSessionLink::threadWanted()
{
    return qEnvironmentVariable("QTCRAFT_SESSION_THREAD") != u"0"_s;
}

void QcSessionLink::start(const QcSessionCommand::Start &start, bool threaded)
{
    if (threaded && start.timer && !m_thread) {
        // Set before the thread runs; deliver() reads it from there.
        m_thread = std::make_unique<QThread>();
        m_thread->setObjectName(u"session tick"_s);
        m_core->moveAllToThread(m_thread.get());
        m_thread->start();
    }
    command({start});
}

void QcSessionLink::command(QcSessionCommand &&command)
{
    if (const auto *intent = std::get_if<QcSessionCommand::SetIntent>(&command.what)) {
        if (m_sentIntent == intent->intent) {
            return;
        }
        m_sentIntent = intent->intent;
    }
    if (command.isMirrored()) {
        ++m_mirroredGiven;
    }

    if (!m_thread) {
        m_core->apply(std::move(command));
        return;
    }

    if (m_commands.push(std::move(command))) {
        QMetaObject::invokeMethod(m_core.get(), [this] { drainCommands(); }, Qt::QueuedConnection);
    }
}

bool QcSessionLink::tick()
{
    Q_ASSERT(!m_thread);
    return m_thread ? false : m_core->tick();
}

QcTickTiming &QcSessionLink::timing() const
{
    return m_core->pacer().timing();
}

void QcSessionLink::deliver(QcSessionEvent &&event)
{
    // In the core's thread when it has one.
    if (!m_thread) {
        m_handler(std::move(event));
        return;
    }

    if (m_events.push(std::move(event))) {
        QMetaObject::invokeMethod(this, &QcSessionLink::drainEvents, Qt::QueuedConnection);
    }
}

void QcSessionLink::drainEvents()
{
    // A nested event loop inside a handler must not overtake the events still in this batch.
    if (m_draining) {
        return;
    }

    m_draining = true;
    const QPointer<QcSessionLink> alive(this);
    for (std::vector<QcSessionEvent> events = m_events.takeAll(); !events.empty(); events = m_events.takeAll()) {
        for (QcSessionEvent &event : events) {
            m_handler(std::move(event));
            if (!alive) {
                // The handler ended the session.
                return;
            }
        }
    }
    m_draining = false;
}

void QcSessionLink::drainCommands()
{
    // In the core's thread.
    for (QcSessionCommand &command : m_commands.takeAll()) {
        m_core->apply(std::move(command));
    }
}

void QcSessionLink::handBack()
{
    // In the core's thread: stops the core and moves it to the link's thread (the destructor waits).
    m_core->stop();
    m_core->moveAllToThread(thread());

    const std::scoped_lock lock(m_handBackMutex);
    m_handedBack = true;
    m_handedBackChanged.notify_one();
}
