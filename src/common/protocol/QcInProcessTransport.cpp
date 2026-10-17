#include "common/protocol/QcInProcessTransport.h"

#include <QMetaObject>

#include <array>
#include <mutex>
#include <vector>

struct QcInProcessTransport::Link
{
    std::mutex mutex;
    std::array<QcInProcessTransport *, 2> ends{};
    std::array<std::vector<QByteArray>, 2> inboxes;
    // Set when the inbox owner has been asked to drain and has not done so yet.
    std::array<bool, 2> wakePending{};
    bool open = true;

    // Precondition: mutex is held.
    void wake(int side)
    {
        QcInProcessTransport *end = ends[std::size_t(side)];
        if (!end || wakePending[std::size_t(side)]) {
            return;
        }
        wakePending[std::size_t(side)] = true;
        // Posted events of a destroyed receiver are discarded; `end` is valid now because the
        // destructor clears it under the same mutex.
        QMetaObject::invokeMethod(end, &QcInProcessTransport::drainInbox, Qt::QueuedConnection);
    }
};

QcInProcessTransport::QcInProcessTransport(std::shared_ptr<Link> link, int side)
    : m_link(std::move(link))
    , m_side(side)
{
}

QcInProcessTransport::~QcInProcessTransport()
{
    close();
    const std::scoped_lock lock(m_link->mutex);
    m_link->ends[std::size_t(m_side)] = nullptr;
}

std::pair<std::unique_ptr<QcInProcessTransport>, std::unique_ptr<QcInProcessTransport>> QcInProcessTransport::createPair()
{
    auto link = std::make_shared<Link>();
    std::unique_ptr<QcInProcessTransport> first(new QcInProcessTransport(link, 0));
    std::unique_ptr<QcInProcessTransport> second(new QcInProcessTransport(link, 1));
    link->ends = {first.get(), second.get()};
    return {std::move(first), std::move(second)};
}

void QcInProcessTransport::sendFrame(const QByteArray &frame)
{
    const std::scoped_lock lock(m_link->mutex);
    if (!m_link->open) {
        return;
    }
    const int peer = 1 - m_side;
    m_link->inboxes[std::size_t(peer)].push_back(frame);
    m_link->wake(peer);
}

void QcInProcessTransport::close()
{
    const std::scoped_lock lock(m_link->mutex);
    if (!m_link->open) {
        return;
    }
    m_link->open = false;
    m_link->wake(0);
    m_link->wake(1);
}

bool QcInProcessTransport::isOpen() const
{
    const std::scoped_lock lock(m_link->mutex);
    return m_link->open;
}

void QcInProcessTransport::drainInbox()
{
    std::vector<QByteArray> frames;
    bool open = true;
    {
        const std::scoped_lock lock(m_link->mutex);
        frames = std::exchange(m_link->inboxes[std::size_t(m_side)], {});
        m_link->wakePending[std::size_t(m_side)] = false;
        open = m_link->open;
    }

    for (const QByteArray &frame : frames) {
        emit frameReceived(frame);
    }

    if (!open && !m_closedEmitted) {
        m_closedEmitted = true;
        emit closed();
    }
}
