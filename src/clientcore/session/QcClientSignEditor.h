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

    // How the editor sends to the server (the session's connection); a different name than
    // QObject::sender().
    void setMessageSender(Sender sender) { m_messageSender = std::move(sender); }
    const Sender &messageSender() const { return m_messageSender; }
    // Takes over OpenSignEditor; false for other messages.
    bool handle(const QcMessage &message);

    bool isOpen() const { return m_open; }
    QcBlockPos pos() const { return m_pos; }
    const QcSignText::Lines &lines() const { return m_lines; }
    // The lines as the player types them (pattern 1 of docs/UI-Leitfaden.md 5: the text holds at
    // once); closing the editor sends them (the server refuses them from a dead player, so on death
    // the typed text is lost). Ignored while no editor is open.
    void setLines(const QcSignText::Lines &lines);
    // Sends `lines` for the open sign and closes the editor; false when none is open.
    bool write(const QcSignText::Lines &lines);

signals:
    void opened();
    void closed();

private:
    void setOpen(bool open) { m_open = open; }

    Sender m_messageSender;
    bool m_open = false;
    QcBlockPos m_pos;
    QcSignText::Lines m_lines;
};
