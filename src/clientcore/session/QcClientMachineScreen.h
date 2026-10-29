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

// The machine or chest screen the server opened for the local player (OpenMachine, MachineState,
// CloseMachine): where it is (or the container minecart it belongs to), its category (empty for a
// storage block or cart), its slots and progress.
// The GUI-free state behind QcClientSession's machine accessors.
class QcClientMachineScreen
{
public:
    enum class Change { None, Opened, Changed, Closed };

    // What a server message did to the screen; std::nullopt for messages about other things.
    std::optional<Change> handle(const QcMessage &message, const QcContentRegistry *content);
    // The player closed the screen; false when none was open. An OpenMachine still on its way is
    // then ignored until the player uses a block again (usedBlock()).
    bool closeLocally();
    void usedBlock() { m_closedLocally = false; }

    bool isOpen() const { return m_pos.has_value(); }
    [[nodiscard]] std::optional<QcBlockPos> pos() const { return m_pos; }
    // The container minecart (entity id) of the open screen, 0 for a block.
    quint32 entity() const { return m_entity; }
    const QString &category() const { return m_category; }
    // The custom name of the open chest or machine (decision E-873); empty for none.
    const QString &name() const { return m_name; }
    // The open screen belongs to a storage block (chest) rather than a machine.
    bool isContainer() const { return m_pos.has_value() && m_category.isEmpty(); }
    // The hotbar slot of a backpack opened from the hand (decision E-833), locked while open; -1
    // for a block or cart.
    int heldSlot() const { return m_heldSlot; }
    const QList<QcItemStack> &stacks() const { return m_slots; }
    double burn() const { return m_burn; }
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
