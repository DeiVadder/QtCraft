// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>

class QThread;

// The thread a session's tick core runs on (decision E-560), named "session tick". A network join
// starts it before it connects, so the TLS socket is made, used and closed in this one thread
// (decision E-872: Qt's OpenSSL backend keeps the socket's TLS state in an object that never
// changes threads). Owned by the thread that made it; run() is the only way in from there.
class QcSessionThread
{
public:
    QcSessionThread();
    // Gives the sockets lingering in the thread their time to leave (QcTcpTransport::lingerAndDelete),
    // then ends the thread and waits for it.
    ~QcSessionThread();

    // Starts the thread. What was set up before (objects moved to thread(), pointers they read
    // there) needs no further hand-over: starting a thread orders it before the thread's work.
    void start();

    QcSessionThread(const QcSessionThread &) = delete;
    QcSessionThread &operator=(const QcSessionThread &) = delete;

    QThread *thread() const { return m_thread.get(); }
    // Runs `job` in the thread after what was queued there before and waits until it returned.
    // Hands over through a std::mutex, so ThreadSanitizer sees that the job's effects come before
    // what the caller does next (Qt's event queue inside the uninstrumented QtCore it does not see).
    // Never from the thread itself; only after start().
    void run(std::function<void()> job);

private:
    void runJob();

    std::unique_ptr<QThread> m_thread;
    // Lives in the thread: the receiver of the queued call of run().
    std::unique_ptr<QObject> m_context;
    std::mutex m_mutex;
    std::condition_variable m_doneChanged;
    std::function<void()> m_job;
    bool m_done = false;
    bool m_started = false;
};
