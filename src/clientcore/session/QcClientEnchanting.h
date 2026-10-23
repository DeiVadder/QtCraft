#pragma once

#include "common/protocol/QcMessages.h"

#include <QList>
#include <QObject>

#include <functional>

// The enchanting table's screen the server opened for the local player (decision E-864): its item
// and lapis slots, the bookshelves that count and the three offers as the server last sent them,
// and the offer the player takes. Items go in and out through the inventory's slot messages (item
// and lapis are the machine's input and fuel slots while the screen is open). Part of
// QcClientSession; GUI-free.
class QcClientEnchanting : public QObject
{
    Q_OBJECT

public:
    using Sender = std::function<void(const QcMessage &)>;

    void setSender(Sender sender) { m_send = std::move(sender); }
    // Takes over OpenEnchanting, EnchantingState and CloseEnchanting; false for other messages.
    bool handle(const QcMessage &message);

    bool isOpen() const { return m_open; }
    QcBlockPos pos() const { return m_pos; }
    const QcMsg::EnchantingState &state() const { return m_state; }
    // Item, lapis and an unused output slot, laid out like a machine's slots for the inventory model.
    [[nodiscard]] QList<QcItemStack> slotStacks() const { return {m_state.item, m_state.lapis, {}}; }
    // Asks the server for offer `slot` (0–2); false when no screen is open or the slot is invalid.
    bool choose(int slot);
    // The player closed the screen; the server gives item and lapis back.
    void close();

signals:
    void opened();
    // Slots, shelves or offers changed.
    void changed();
    void closed();

private:
    Sender m_send;
    bool m_open = false;
    QcBlockPos m_pos;
    QcMsg::EnchantingState m_state;
};
