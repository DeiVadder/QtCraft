#include "common/core/QcWorkerPool.h"

#include "common/diagnostics/QcCrashHandler.h"

#include <QThread>

#include <algorithm>

using namespace Qt::StringLiterals;

QcWorkerPool::QcWorkerPool(int threadCount)
{
    for (int i = 0; i < std::max(1, threadCount); ++i) {
        std::unique_ptr<QThread> thread(QThread::create([this] {
            QcCrashHandler::prepareThread();
            run();
        }));
        thread->setObjectName(u"QtCraft worker %1"_s.arg(i));
        thread->start();
        m_threads.push_back(std::move(thread));
    }
}

QcWorkerPool::~QcWorkerPool()
{
    {
        const std::scoped_lock lock(m_mutex);
        m_stopping = true;
        m_jobs.clear();
    }
    m_wake.notify_all();

    for (const std::unique_ptr<QThread> &thread : m_threads) {
        thread->wait();
    }
    m_threads.clear();

    // Every worker released the mutex last when it returned. Taking it once more makes that
    // visible to ThreadSanitizer, which cannot see the join inside QThread::wait().
    const std::scoped_lock lock(m_mutex);
}

void QcWorkerPool::submit(std::function<void()> job)
{
    {
        const std::scoped_lock lock(m_mutex);
        m_jobs.push_back(std::move(job));
    }
    m_wake.notify_one();
}

void QcWorkerPool::submitFirst(std::function<void()> job)
{
    {
        const std::scoped_lock lock(m_mutex);
        m_jobs.push_front(std::move(job));
    }
    m_wake.notify_one();
}

void QcWorkerPool::waitForIdle()
{
    std::unique_lock lock(m_mutex);
    m_idle.wait(lock, [this] { return m_jobs.empty() && m_running == 0; });
}

qsizetype QcWorkerPool::discardQueued()
{
    std::deque<std::function<void()>> dropped;
    {
        const std::scoped_lock lock(m_mutex);
        dropped.swap(m_jobs);
        if (m_running == 0) {
            m_idle.notify_all();
        }
    }

    // The jobs' captures are released outside the lock.
    return static_cast<qsizetype>(dropped.size());
}

void QcWorkerPool::run()
{
    std::unique_lock lock(m_mutex);
    while (true) {
        m_wake.wait(lock, [this] { return m_stopping || !m_jobs.empty(); });
        if (m_stopping) {
            return;
        }

        const std::function<void()> job = std::move(m_jobs.front());
        m_jobs.pop_front();
        ++m_running;
        lock.unlock();
        job();
        lock.lock();

        --m_running;
        if (m_jobs.empty() && m_running == 0) {
            m_idle.notify_all();
        }
    }
}
