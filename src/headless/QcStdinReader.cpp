// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "headless/QcStdinReader.h"

#include <QThread>

#include <cerrno>
#include <cstdio>
#include <mutex>
#include <vector>

#if !defined(Q_OS_WIN)
#include <unistd.h>
#endif

namespace {

// The next bytes of the standard input, empty at its end or on an error. Outside Windows this reads
// the file descriptor, not the C library's stdin: fgets() holds the lock of stdin while it waits,
// and glibc's exit() takes that lock to flush the streams, so a server stopped from its console
// hung at exit on Linux (E-949). Windows keeps fgets(), whose console input the CRT translates.
QByteArray readStdinChunk()
{
    char buffer[4096];
#if defined(Q_OS_WIN)
    if (!std::fgets(buffer, sizeof buffer, stdin)) {
        return {};
    }
    return QByteArray(buffer);
#else
    for (;;) {
        const ssize_t count = ::read(STDIN_FILENO, buffer, sizeof buffer);
        if (count > 0) {
            return QByteArray(buffer, static_cast<qsizetype>(count));
        }
        if (count == 0 || errno != EINTR) {
            return {};
        }
    }
#endif
}

} // namespace

struct QcStdinReader::Shared
{
    // The reading thread hands the lines to the receiver's thread under this mutex.
    std::mutex mutex;
    // Cleared by the reader's destructor; the thread then stops delivering.
    QcStdinReader *receiver = nullptr;
    std::vector<QByteArray> lines;
    bool endOfInput = false;
    bool wakePending = false;

    // Precondition: mutex is held.
    void wake()
    {
        if (receiver && !wakePending) {
            wakePending = true;
            QMetaObject::invokeMethod(receiver, &QcStdinReader::deliver, Qt::QueuedConnection);
        }
    }
};

QcStdinReader::QcStdinReader(QObject *parent)
    : QObject(parent)
    , m_shared(std::make_shared<Shared>())
{
    m_shared->receiver = this;
}

QcStdinReader::~QcStdinReader()
{
    const std::scoped_lock lock(m_shared->mutex);
    m_shared->receiver = nullptr;
}

void QcStdinReader::start()
{
    // The thread blocks in a read of the standard input and cannot be interrupted portably, so it is
    // not joined: it ends with the process. It only touches the shared state, which it co-owns.
    QThread *thread = QThread::create([shared = m_shared] {
        QByteArray line;
        for (QByteArray chunk = readStdinChunk(); !chunk.isEmpty(); chunk = readStdinChunk()) {
            // Only the new chunk can hold a newline: what came before it held none.
            const qsizetype inChunk = chunk.lastIndexOf('\n');
            line.append(chunk);
            if (inChunk < 0) {
                continue;
            }
            const qsizetype end = line.size() - chunk.size() + inChunk;

            // Each complete line with its newline, as the console expects them.
            const QByteArray rest = line.mid(end + 1);
            line.truncate(end + 1);
            const std::scoped_lock lock(shared->mutex);
            for (qsizetype from = 0; from < line.size();) {
                const qsizetype next = line.indexOf('\n', from) + 1;
                shared->lines.push_back(line.mid(from, next - from));
                from = next;
            }
            line = rest;
            shared->wake();
        }

        const std::scoped_lock lock(shared->mutex);
        if (!line.isEmpty()) {
            shared->lines.push_back(line);
        }
        shared->endOfInput = true;
        shared->wake();
    });

    thread->setObjectName(QStringLiteral("QtCraft stdin"));
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void QcStdinReader::deliver()
{
    std::vector<QByteArray> lines;
    bool finished = false;
    {
        const std::scoped_lock lock(m_shared->mutex);
        lines = std::exchange(m_shared->lines, {});
        finished = m_shared->endOfInput;
        m_shared->wakePending = false;
    }

    for (const QByteArray &line : lines) {
        emit lineReceived(line);
    }
    if (finished) {
        emit endOfInput();
    }
}
