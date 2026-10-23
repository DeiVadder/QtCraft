#pragma once

#include "common/protocol/QcMessages.h"

#include <QList>
#include <QObject>

#include <functional>

// The anvil's screen the server opened for the local player (decision E-866): its left and right
// slots, the name in its field, the output and its cost as the server last sent them; the player
// renames and takes the output. Items go in and out through the inventory's slot messages (left and
// right are the machine's input and fuel slots while the screen is open). Part of QcClientSession;
// GUI-free.
class QcClientAnvil : public QObject
{
    Q_OBJECT

public:
    using Sender = std::function<void(const QcMessage &)>;

    void setSender(Sender sender) { m_send = std::move(sender); }
    // Takes over OpenAnvil, AnvilState and CloseAnvil; false for other messages.
    bool handle(const QcMessage &message);

    bool isOpen() const { return m_open; }
    QcBlockPos pos() const { return m_pos; }
    const QcMsg::AnvilState &state() const { return m_state; }
    // Left, right and the output, laid out like a machine's slots for the inventory model.
    [[nodiscard]] QList<QcItemStack> slotStacks() const { return {m_state.left, m_state.right, m_state.output}; }
    // Sends the text of the name field; false when no screen is open.
    bool rename(const QString &name);
    // Asks the server for the output and cost it last sent; false when there is none.
    bool forge();
    // The player closed the screen; the server gives both slots back.
    void close();

signals:
    void opened();
    // Slots, name, output or cost changed.
    void changed();
    void closed();

private:
    Sender m_send;
    bool m_open = false;
    QcBlockPos m_pos;
    QcMsg::AnvilState m_state;
};
