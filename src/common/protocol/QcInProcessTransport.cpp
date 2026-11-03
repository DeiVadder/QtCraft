// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/protocol/QcInProcessTransport.h"

#include <QMetaObject>

#include <array>
#include <mutex>
#include <vector>

struct QcInProcessTransport::Link
{
    // The threads of the two ends (server thread and session tick thread) hand frames over under
    // this mutex.
    std::mutex mutex;
    std::array<QcInProcessTransport *, 2> ends{};
    std::array<std::vector<QByteArray>, 2> inboxes;
    // Set when the inbox owner has been asked to drain and has not done so yet.
    std::array<bool, 2> wakePending{};
    bool open = true;

    // Precondition: mutex is held.
    void wake(End which)
    {
        QcInProcessTransport *end = ends[slotOf(which)];
        if (!end || wakePending[slotOf(which)]) {
            return;
        }
        wakePending[slotOf(which)] = true;
        // Posted events of a destroyed receiver are discarded; `end` is valid now because the
        // destructor clears it under the same mutex.
        QMetaObject::invokeMethod(end, &QcInProcessTransport::drainInbox, Qt::QueuedConnection);
    }
};

QcInProcessTransport::QcInProcessTransport(std::shared_ptr<Link> link, End end)
    : m_link(std::move(link))
    , m_end(end)
{
}

QcInProcessTransport::~QcInProcessTransport()
{
    close();
    const std::scoped_lock lock(m_link->mutex);
    m_link->ends[slotOf(m_end)] = nullptr;
}

std::pair<std::unique_ptr<QcInProcessTransport>, std::unique_ptr<QcInProcessTransport>> QcInProcessTransport::createPair()
{
    auto link = std::make_shared<Link>();
    std::unique_ptr<QcInProcessTransport> first(new QcInProcessTransport(link, End::First));
    std::unique_ptr<QcInProcessTransport> second(new QcInProcessTransport(link, End::Second));
    link->ends = {first.get(), second.get()};
    return {std::move(first), std::move(second)};
}

void QcInProcessTransport::sendFrame(const QByteArray &frame)
{
    const std::scoped_lock lock(m_link->mutex);
    if (!m_link->open) {
        return;
    }
    const End peer = peerOf(m_end);
    m_link->inboxes[slotOf(peer)].push_back(frame);
    m_link->wake(peer);
}

void QcInProcessTransport::close()
{
    const std::scoped_lock lock(m_link->mutex);
    if (!m_link->open) {
        return;
    }
    m_link->open = false;
    m_link->wake(End::First);
    m_link->wake(End::Second);
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
        frames = std::exchange(m_link->inboxes[slotOf(m_end)], {});
        m_link->wakePending[slotOf(m_end)] = false;
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
