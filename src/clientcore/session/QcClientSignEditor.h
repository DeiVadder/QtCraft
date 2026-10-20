#pragma once

#include "common/protocol/QcMessages.h"

#include <QObject>

#include <functional>

// The editor of a sign the server opened for the local player (decision E-766): the sign's
// position and lines (QcMsg::OpenSignEditor), and the lines the player writes (QcMsg::EditSign).
// The server checks and cleans them; the text everyone sees comes back as QcMsg::SignText into the
// world mirror. Part of QcClientSession; GUI-free.
class QcClientSignEditor : public QObject
{
    Q_OBJECT

public:
    using Sender = std::function<void(const QcMessage &)>;

    void setSender(Sender sender) { m_send = std::move(sender); }
    // Takes over OpenSignEditor; false for other messages.
    bool handle(const QcMessage &message);

    bool isOpen() const { return m_open; }
    QcBlockPos pos() const { return m_pos; }
    const QcSignText::Lines &lines() const { return m_lines; }
    // Sends `lines` for the open sign and closes the editor; false when none is open.
    bool write(const QcSignText::Lines &lines);

signals:
    void opened();
    void closed();

private:
    Sender m_send;
    bool m_open = false;
    QcBlockPos m_pos;
    QcSignText::Lines m_lines;
};
