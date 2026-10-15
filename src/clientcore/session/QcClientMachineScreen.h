#pragma once

#include "QcBlockPos.h"
#include "QcItemStack.h"
#include "QcMessages.h"

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
    // The open screen belongs to a storage block (chest) rather than a machine.
    bool isContainer() const { return m_pos.has_value() && m_category.isEmpty(); }
    const QList<QcItemStack> &stacks() const { return m_slots; }
    double burn() const { return m_burn; }
    double progress() const { return m_progress; }

private:
    Change open(const QcMsg::OpenMachine &open);
    Change update(const QcMsg::MachineState &machine, const QcContentRegistry *content);

    std::optional<QcBlockPos> m_pos;
    quint32 m_entity = 0;
    // The player closed a screen and has not used a block since.
    bool m_closedLocally = false;
    QString m_category;
    QList<QcItemStack> m_slots = QList<QcItemStack>(QcMsg::MachineState::slotCount);
    double m_burn = 0.0;
    double m_progress = 0.0;
};
