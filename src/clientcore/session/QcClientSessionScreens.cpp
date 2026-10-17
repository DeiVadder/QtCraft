// The screens of a client session: crafting, machine and chest screens and moving items between
// their slots and the inventory (the trading screen is QcClientTrading's).

#include "clientcore/session/QcClientSession.h"

#include "common/protocol/QcConnection.h"

bool QcClientSession::handleMachineMessage(const QcMessage &message)
{
    const std::optional<QcClientMachineScreen::Change> change = m_machine.handle(message, m_content.get());
    if (!change) {
        return false;
    }

    switch (*change) {
    case QcClientMachineScreen::Change::Opened:
        emit machineOpened();
        break;
    case QcClientMachineScreen::Change::Changed:
        emit machineChanged();
        break;
    case QcClientMachineScreen::Change::Closed:
        emit machineClosed();
        break;
    case QcClientMachineScreen::Change::None:
        break;
    }
    return true;
}

void QcClientSession::takeCraftingResult()
{
    m_connection->send(QcMsg::TakeCraftingResult{});
}

void QcClientSession::craftRecipe(const QString &recipe, int count)
{
    m_connection->send(QcMsg::CraftRecipe{recipe, count});
}

void QcClientSession::closeCrafting()
{
    m_connection->send(QcMsg::CloseCrafting{});
}

void QcClientSession::closeMachine()
{
    if (!m_machine.closeLocally()) {
        return;
    }

    m_connection->send(QcMsg::CloseMachine{});
    emit machineClosed();
}

void QcClientSession::selectHotbarSlot(int slot)
{
    m_inventory.setSelectedHotbarSlot(slot);
    m_connection->send(QcMsg::SelectHotbarSlot{m_inventory.selectedHotbarSlot()});
    emit inventoryChanged();
}

void QcClientSession::moveInventoryItem(int from, int to, int count)
{
    m_connection->send(QcMsg::MoveInventoryItem{from, to, count});
}

void QcClientSession::quickMoveItem(int slot)
{
    m_connection->send(QcMsg::QuickMoveItem{slot});
}
