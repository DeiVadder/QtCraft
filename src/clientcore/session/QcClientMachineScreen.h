// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"
#include "common/gameplay/QcItemStack.h"
#include "common/protocol/QcMessages.h"

#include <QList>
#include <QString>

#include <optional>

class QcContentRegistry;

/*! \brief The machine or chest screen the server opened for the local player (OpenMachine,
    MachineState, CloseMachine): where it is (or the container minecart it belongs to), its category
    (empty for a storage block or cart), its slots and progress.

    The GUI-free state behind QcClientSession's machine accessors. */
class QcClientMachineScreen
{
public:
    /*! The category of a brewing stand (QcMachineDefinition::category). */
    static constexpr auto brewingCategory = "brewing";

    /*! What a server message did to the screen. */
    enum class Change {
        None,    // a message about a screen that changed nothing here
        Opened,  // the server opened a screen
        Changed, // slots or progress of the open screen changed
        Closed   // the server closed the screen
    };

    /*! Returns what the server \a message did to the screen (items checked against \a content);
        std::nullopt for messages about other things. */
    std::optional<Change> handle(const QcMessage &message, const QcContentRegistry *content);
    /*! The player closed the screen. Returns false when none was open. An OpenMachine still on
        its way is then ignored until the player uses a block again (usedBlock()). */
    bool closeLocally();
    /*! The player used a block: OpenMachine messages count again (see closeLocally()). */
    void usedBlock() { m_closedLocally = false; }

    /*! Returns whether a screen is open. */
    bool isOpen() const { return m_pos.has_value(); }
    /*! Returns where the open machine or chest is; for a cart, where it was when it opened; nothing
        while closed. */
    [[nodiscard]] std::optional<QcBlockPos> pos() const { return m_pos; }
    /*! Returns the container minecart (entity id) of the open screen, 0 for a block. */
    quint32 entity() const { return m_entity; }
    /*! Returns the category of the open machine (content name); empty for a storage block or cart.
     */
    const QString &category() const { return m_category; }
    /*! Returns the custom name of the open chest or machine (decision E-873); empty for none. */
    const QString &name() const { return m_name; }
    /*! Returns whether the open screen belongs to a storage block (chest) rather than a machine. */
    bool isContainer() const { return m_pos.has_value() && m_category.isEmpty(); }
    /*! Returns whether the open screen belongs to a brewing stand (decision E-1063), with five
        slots: ingredient, fuel and three bottles. */
    bool isBrewing() const { return m_category == QLatin1StringView(brewingCategory); }
    /*! Returns the hotbar slot of a backpack opened from the hand (decision E-833), locked while
        open; -1 for a block or cart. */
    int heldSlot() const { return m_heldSlot; }
    /*! Returns the slots of the open screen as the server last sent them. */
    const QList<QcItemStack> &stacks() const { return m_slots; }
    /*! Returns the fuel left of the open machine (0..1). */
    double burn() const { return m_burn; }
    /*! Returns the progress of the open machine's current job (0..1). */
    double progress() const { return m_progress; }

private:
    Change open(const QcMsg::OpenMachine &open);
    Change update(const QcMsg::MachineState &machine, const QcContentRegistry *content);

    std::optional<QcBlockPos> m_pos;
    quint32 m_entity = 0;
    int m_heldSlot = QcMsg::OpenMachine::noHeldSlot;
    // The player closed a screen and has not used a block since.
    bool m_closedLocally = false;
    QString m_category;
    QString m_name;
    QList<QcItemStack> m_slots = QList<QcItemStack>(QcMsg::MachineState::slotCount);
    double m_burn = 0.0;
    double m_progress = 0.0;
};
