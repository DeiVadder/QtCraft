#pragma once

#include <QList>

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>

class QThread;

// Fixed set of worker threads executing queued jobs in FIFO order. Jobs are handed over through
// std::mutex/std::condition_variable so ThreadSanitizer can follow the data (see QcMailbox).
class QcWorkerPool
{
public:
    explicit QcWorkerPool(int threadCount);
    ~QcWorkerPool();

    QcWorkerPool(const QcWorkerPool &) = delete;
    QcWorkerPool &operator=(const QcWorkerPool &) = delete;

    void submit(std::function<void()> job);
    // Puts the job before every job that has not started yet.
    void submitFirst(std::function<void()> job);
    // Blocks until the queue is empty and no job is running.
    void waitForIdle();
    // Drops the jobs that have not started yet and returns how many; running jobs go on.
    qsizetype discardQueued();
    int threadCount() const { return int(m_threads.size()); }

private:
    void run();

    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::condition_variable m_idle;
    std::deque<std::function<void()>> m_jobs;
    int m_running = 0;
    bool m_stopping = false;
    QList<QThread *> m_threads;
};
