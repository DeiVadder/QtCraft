#include "QcStdinReader.h"

#include <QThread>

#include <cstdio>
#include <mutex>
#include <vector>

struct QcStdinReader::Shared
{
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
    // The thread blocks in fgets() and cannot be interrupted portably, so it is not joined: it ends
    // with the process. It only touches the shared state, which it co-owns.
    QThread *thread = QThread::create([shared = m_shared] {
        QByteArray line;
        char buffer[4096];
        while (std::fgets(buffer, sizeof buffer, stdin)) {
            line.append(buffer);
            if (!line.endsWith('\n')) {
                continue;
            }

            const std::scoped_lock lock(shared->mutex);
            shared->lines.push_back(std::exchange(line, {}));
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
