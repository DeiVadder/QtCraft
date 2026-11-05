// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/protocol/QcMessages.h"

#include <QList>
#include <QObject>
#include <QString>

#include <functional>

class QcContentRegistry;

/*! \brief The stonecutter screen the server opened for the local player (decision E-785): its input
    slot and the stonecutting recipe chosen for it (decision E-784) as the server last sent them,
    and the choices the player makes.

    Items go in and out through the inventory's slot messages (input and output are the machine
    slots while the screen is open). Part of QcClientSession; GUI-free. */
class QcClientStonecutter : public QObject
{
    Q_OBJECT

public:
    /*! Sends a message to the server. */
    using Sender = std::function<void(const QcMessage &)>;

    /*! Sets the way out to the server to \a sender (the session's connection). */
    void setSender(Sender sender) { m_send = std::move(sender); }
    /*! Takes over \a message when it is OpenStonecutter, StonecutterState or CloseStonecutter.
        Returns false for other messages. */
    bool handle(const QcMessage &message);

    /*! Returns whether the screen is open. */
    bool isOpen() const { return m_open; }
    /*! Returns where the stonecutter is. */
    QcBlockPos pos() const { return m_pos; }
    /*! Returns the stack in the input slot as the server last sent it. */
    QcItemStack input() const { return m_input; }
    /*! Returns the chosen recipe's name; empty for none. */
    const QString &recipe() const { return m_recipe; }
    /*! Returns the input, an unused middle slot and the output, with \a content, laid out like a
        machine's slots for the inventory model (the output follows from input and recipe,
        QcStonecutting::output). */
    [[nodiscard]] QList<QcItemStack> slotStacks(const QcContentRegistry &content) const;

    /*! Asks the server to choose the recipe \a recipe (empty: none). Returns false when no
        screen is open. */
    bool select(const QString &recipe);
    /*! The player closed the screen; the server gives the input back. */
    void close();

signals:
    /*! The server opened the screen. */
    void opened();
    /*! The input or the chosen recipe changed. */
    void changed();
    /*! The screen closed (by the server or by close()). */
    void closed();

private:
    Sender m_send;
    bool m_open = false;
    QcBlockPos m_pos;
    QcItemStack m_input;
    QString m_recipe;
};
