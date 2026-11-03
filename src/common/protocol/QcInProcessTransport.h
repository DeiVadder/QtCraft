// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/protocol/QcTransport.h"

#include <QtGlobal>

#include <cstddef>
#include <memory>
#include <utility>

/*! \brief One end of an in-process link.

    Frames go into the peer's inbox under a mutex and the peer is woken through its event loop,
    so both ends may live in different threads (server and GUI). */
class QcInProcessTransport : public QcTransport
{
    Q_OBJECT

public:
    ~QcInProcessTransport() override;
    Q_DISABLE_COPY_MOVE(QcInProcessTransport)

    /*! Creates and returns two connected ends without parents; move each to its thread before
        use. */
    static std::pair<std::unique_ptr<QcInProcessTransport>, std::unique_ptr<QcInProcessTransport>> createPair();

    void sendFrame(const QByteArray &frame) override;
    void close() override;
    bool isOpen() const override;

private:
    struct Link;
    // The two ends of a link; the value is the end's slot in the link's arrays.
    enum class End : quint8
    {
        First = 0,
        Second = 1,
    };

    // The slot of `end` in the arrays of its link.
    static constexpr std::size_t slotOf(End end) { return std::to_underlying(end); }
    // The other end of the link.
    static constexpr End peerOf(End end) { return end == End::First ? End::Second : End::First; }

    QcInProcessTransport(std::shared_ptr<Link> link, End end);
    void drainInbox();

    std::shared_ptr<Link> m_link;
    End m_end = End::First;
    bool m_closedEmitted = false;
};
