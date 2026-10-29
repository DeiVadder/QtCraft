// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/tick/QcSessionThread.h"

#include "common/diagnostics/QcCrashHandler.h"
#include "network/QcTcpTransport.h"

#include <QThread>

using namespace Qt::StringLiterals;

QcSessionThread::QcSessionThread()
    : m_thread(std::make_unique<QThread>())
    , m_context(std::make_unique<QObject>())
{
    m_thread->setObjectName(u"session tick"_s);
    QObject::connect(m_thread.get(), &QThread::started, m_thread.get(), &QcCrashHandler::prepareThread, Qt::DirectConnection);
    m_context->moveToThread(m_thread.get());
}

void QcSessionThread::start()
{
    m_started = true;
    m_thread->start();
}

QcSessionThread::~QcSessionThread()
{
    if (!m_started) {
        return;
    }

    // A TCP socket closed in the thread may still send its last frames (the reason of a
    // Disconnect); the thread waits for them, at most 1 s.
    run([] { QcTcpTransport::finishLingering(); });
    m_thread->quit();
    m_thread->wait();
}

void QcSessionThread::run(std::function<void()> job)
{
    Q_ASSERT(m_started && QThread::currentThread() != m_thread.get());
    {
        const std::scoped_lock lock(m_mutex);
        m_job = std::move(job);
        m_done = false;
    }
    // The queued call carries only `this` (tests/tsan.supp); the job goes through the mutex.
    QMetaObject::invokeMethod(m_context.get(), [this] { runJob(); }, Qt::QueuedConnection);

    std::unique_lock lock(m_mutex);
    m_doneChanged.wait(lock, [this] { return m_done; });
}

void QcSessionThread::runJob()
{
    // In the thread.
    std::function<void()> job;
    {
        const std::scoped_lock lock(m_mutex);
        job = std::exchange(m_job, {});
    }
    job();

    // Signalled while holding the mutex: the last access to this object from the thread.
    const std::scoped_lock lock(m_mutex);
    m_done = true;
    m_doneChanged.notify_one();
}
