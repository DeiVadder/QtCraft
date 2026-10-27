#pragma once

#include "common/protocol/QcTransport.h"

#include <QtGlobal>

#include <memory>
#include <utility>

// One end of an in-process link. Frames go into the peer's inbox under a mutex and the peer is
// woken through its event loop, so both ends may live in different threads (server and GUI).
class QcInProcessTransport : public QcTransport
{
    Q_OBJECT

public:
    ~QcInProcessTransport() override;
    Q_DISABLE_COPY_MOVE(QcInProcessTransport)

    // Creates two connected ends without parents; move each to its thread before use.
    static std::pair<std::unique_ptr<QcInProcessTransport>, std::unique_ptr<QcInProcessTransport>> createPair();

    void sendFrame(const QByteArray &frame) override;
    void close() override;
    bool isOpen() const override;

private:
    struct Link;

    QcInProcessTransport(std::shared_ptr<Link> link, int side);
    void drainInbox();

    std::shared_ptr<Link> m_link;
    int m_side = 0;
    bool m_closedEmitted = false;
};
