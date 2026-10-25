#include "clientcore/tick/QcSessionLink.h"

#include "clientcore/debug/QcClientLogging.h"
#include "clientcore/tick/QcSessionCore.h"
#include "clientcore/tick/QcSessionThread.h"
#include "common/protocol/QcTransport.h"

#include <QPointer>
#include <QThread>

using namespace Qt::StringLiterals;

QcSessionLink::QcSessionLink(std::unique_ptr<QcTransport> transport, Handler handler)
    : m_handler(std::move(handler))
    , m_core(std::make_unique<QcSessionCore>(std::move(transport), [this](QcSessionEvent &&event) { deliver(std::move(event)); }))
{
}

QcSessionLink::QcSessionLink(std::unique_ptr<QcSessionThread> thread, std::unique_ptr<QcTransport> transport, Handler handler)
    : m_handler(std::move(handler))
    , m_core(std::make_unique<QcSessionCore>(nullptr, [this](QcSessionEvent &&event) { deliver(std::move(event)); }))
    , m_thread(std::move(thread))
{
    // Without a connection the core moves freely; the transport stays where it was made.
    m_core->moveAllToThread(m_thread->thread());
    m_thread->run([this, &transport] { m_core->attach(std::move(transport)); });
}

QcSessionLink::~QcSessionLink()
{
    if (!m_thread) {
        return;
    }

    // Queued after the wake-ups of the commands given before, so those still reach the core. The
    // core stops and closes its connection in its thread (a TLS socket never leaves it, decision
    // E-872) and comes back without it; nothing calls deliver() any more. The thread ends only
    // after a TCP socket's last frames left (~QcSessionThread, at most the linger time).
    m_thread->run([this, linkThread = thread()] {
        m_core->shutdown();
        m_core->moveAllToThread(linkThread);
    });
    m_thread.reset();
}

bool QcSessionLink::threadWanted()
{
    return qEnvironmentVariable("QTCRAFT_SESSION_THREAD") != u"0"_s;
}

void QcSessionLink::start(const QcSessionCommand::Start &start, bool threaded)
{
    if (threaded && start.timer && !m_thread) {
        if (m_core->transportKeepsItsThread()) {
            qCWarning(lcSession) << "The session ticks in its own thread: its transport cannot move to a tick thread";
        } else {
            // Set and moved before the thread starts: a transport's queued work may run there at
            // once, and deliver() reads m_thread from there.
            m_thread = std::make_unique<QcSessionThread>();
            m_core->moveAllToThread(m_thread->thread());
            m_thread->start();
        }
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

QcLinkStats &QcSessionLink::linkStats() const
{
    return m_core->linkStats();
}

std::optional<QcRoundTripSummary> QcSessionLink::latency() const
{
    return m_core->latency().summary();
}

void QcSessionLink::resetLatency()
{
    m_core->latency().reset();
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
