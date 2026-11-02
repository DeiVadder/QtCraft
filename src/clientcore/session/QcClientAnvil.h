// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/protocol/QcMessages.h"

#include <QList>
#include <QObject>

#include <functional>

/*! \brief The anvil's screen the server opened for the local player (decision E-866): its left and
    right slots, the name in its field, the output and its cost as the server last sent them; the
    player renames and takes the output.

    Items go in and out through the inventory's slot messages (left and right are the machine's
    input and fuel slots while the screen is open). Part of QcClientSession; GUI-free. */
class QcClientAnvil : public QObject
{
    Q_OBJECT

public:
    /*! Sends a message to the server. */
    using Sender = std::function<void(const QcMessage &)>;

    /*! Sets the way out to the server to \a sender (the session's connection). */
    void setSender(Sender sender) { m_send = std::move(sender); }
    /*! Takes over \a message when it is OpenAnvil, AnvilState or CloseAnvil. Returns false for
        other messages. */
    bool handle(const QcMessage &message);

    /*! Returns whether the screen is open. */
    bool isOpen() const { return m_open; }
    /*! Returns where the anvil is. */
    QcBlockPos pos() const { return m_pos; }
    /*! Returns the slots, name, output and cost as the server last sent them. */
    const QcMsg::AnvilState &state() const { return m_state; }
    /*! Returns the left and right slot and the output, laid out like a machine's slots for the
        inventory model. */
    [[nodiscard]] QList<QcItemStack> slotStacks() const { return {m_state.left, m_state.right, m_state.output}; }
    /*! Sends \a name, the text of the name field. Returns false when no screen is open. */
    bool rename(const QString &name);
    /*! Asks the server for the output and cost it last sent. Returns false when there is none. */
    bool forge();
    /*! The player closed the screen; the server gives both slots back. */
    void close();

signals:
    /*! The server opened the screen. */
    void opened();
    /*! Slots, name, output or cost changed. */
    void changed();
    /*! The screen closed (by the server or by close()). */
    void closed();

private:
    Sender m_send;
    bool m_open = false;
    QcBlockPos m_pos;
    QcMsg::AnvilState m_state;
};
