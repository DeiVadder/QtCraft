#include "QcClientMachineScreen.h"

#include "QcContentRegistry.h"

#include <algorithm>

std::optional<QcClientMachineScreen::Change> QcClientMachineScreen::handle(const QcMessage &message, const QcContentRegistry *content)
{
    if (const auto *opened = std::get_if<QcMsg::OpenMachine>(&message)) {
        return open(*opened);
    }
    if (const auto *machine = std::get_if<QcMsg::MachineState>(&message)) {
        return update(*machine, content);
    }
    if (!std::holds_alternative<QcMsg::CloseMachine>(message)) {
        return std::nullopt;
    }

    if (!m_pos) {
        return Change::None;
    }
    m_pos.reset();
    return Change::Closed;
}

QcClientMachineScreen::Change QcClientMachineScreen::open(const QcMsg::OpenMachine &open)
{
    // An answer to a use that the player already closed again; the server has closed it too.
    if (m_closedLocally) {
        return Change::None;
    }

    m_pos = open.pos;
    m_category = open.category;
    m_slots = QList<QcItemStack>(open.category.isEmpty() ? 0 : QcMsg::MachineState::slotCount);
    m_burn = 0.0;
    m_progress = 0.0;
    return Change::Opened;
}

QcClientMachineScreen::Change QcClientMachineScreen::update(const QcMsg::MachineState &machine, const QcContentRegistry *content)
{
    // Only the open machine's state is shown; unknown items would break the screen.
    const bool known = std::ranges::all_of(machine.stacks, [content](QcItemStack stack) {
        return stack.isEmpty() || (content && content->isValidItem(stack.item));
    });
    if (m_pos != machine.pos || !known) {
        return Change::None;
    }

    m_slots = machine.stacks;
    m_burn = machine.burn;
    m_progress = machine.progress;
    return Change::Changed;
}

bool QcClientMachineScreen::closeLocally()
{
    if (!m_pos) {
        return false;
    }

    m_pos.reset();
    m_closedLocally = true;
    return true;
}
