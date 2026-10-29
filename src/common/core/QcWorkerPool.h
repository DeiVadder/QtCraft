// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtGlobal>

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

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
    int threadCount() const { return static_cast<int>(m_threads.size()); }

private:
    void run();

    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::condition_variable m_idle;
    std::deque<std::function<void()>> m_jobs;
    int m_running = 0;
    bool m_stopping = false;
    std::vector<std::unique_ptr<QThread>> m_threads;
};
