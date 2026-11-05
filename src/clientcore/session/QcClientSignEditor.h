// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/protocol/QcMessages.h"

#include <QObject>

#include <functional>

/*! \brief The editor of a sign the server opened for the local player (decision E-766): the sign's
    position and lines (QcMsg::OpenSignEditor), and the lines the player writes (QcMsg::EditSign).

    The server checks and cleans them; the text everyone sees comes back as QcMsg::SignText into
    the world mirror. Part of QcClientSession; GUI-free. */
class QcClientSignEditor : public QObject
{
    Q_OBJECT

public:
    /*! Sends a message to the server. */
    using Sender = std::function<void(const QcMessage &)>;

    /*! Sets \a sender, how the editor sends to the server (the session's connection); a
        different name than QObject::sender(). */
    void setMessageSender(Sender sender) { m_messageSender = std::move(sender); }
    /*! Returns how the editor sends to the server. */
    const Sender &messageSender() const { return m_messageSender; }
    /*! Takes over \a message when it is OpenSignEditor. Returns false for other messages. */
    bool handle(const QcMessage &message);

    /*! Returns whether an editor is open. */
    bool isOpen() const { return m_open; }
    /*! Returns where the sign is. */
    QcBlockPos pos() const { return m_pos; }
    /*! Returns the lines as the server opened them or the player typed them. */
    const QcSignText::Lines &lines() const { return m_lines; }
    /*! Sets \a lines, the lines as the player types them (pattern 1 of docs/UI-Leitfaden.md 5:
        the text holds at once); closing the editor sends them (the server refuses them from a
        dead player, so on death the typed text is lost). Ignored while no editor is open. */
    void setLines(const QcSignText::Lines &lines);
    /*! Sends \a lines for the open sign and closes the editor. Returns false when none is open. */
    bool write(const QcSignText::Lines &lines);

signals:
    /*! The server opened the editor of a sign. */
    void opened();
    /*! The editor closed and sent its lines (write()). */
    void closed();

private:
    void setOpen(bool open) { m_open = open; }

    Sender m_messageSender;
    bool m_open = false;
    QcBlockPos m_pos;
    QcSignText::Lines m_lines;
};
