#include "QcControlMachineCommands.h"

#include "QcClientSession.h"
#include "QcContentRegistry.h"
#include "QcControlAim.h"
#include "QcControlArguments.h"
#include "QcControlCommandTable.h"
#include "QcControlContext.h"

#include <algorithm>

using namespace Qt::StringLiterals;

QcControlMachineCommands::QcControlMachineCommands(QcControlContext &context)
    : m_context(context)
{
}

void QcControlMachineCommands::registerCommands(QcControlCommandTable &table)
{
    table.add(u"machine"_s, [this](const QJsonObject &request) { machine(request); });
    table.add(u"openMachine"_s, [this](const QJsonObject &request) { openMachine(request); });
    table.add(u"fillMachine"_s, [this](const QJsonObject &request) { fillMachine(request); });
    table.add(u"takeOutput"_s, [this](const QJsonObject &request) { takeOutput(request); });
    table.add(u"closeMachine"_s, [this](const QJsonObject &request) { closeMachine(request); });
}

QcClientSession &QcControlMachineCommands::session() const
{
    return *m_context.session();
}

void QcControlMachineCommands::machine(const QJsonObject &request)
{
    m_context.finish(request, m_context.reports().machineJson());
}

void QcControlMachineCommands::openMachine(const QJsonObject &request)
{
    // Looks at the block and uses it like a player; answers once the server opened the screen.
    const quint64 startTick = session().tickCount();
    if (!request.contains(u"x"_s)) {
        m_context.fail(request, u"'x', 'y' and 'z' are required"_s);
        return;
    }

    const QcBlockPos pos = QcControlArguments::block(request);
    QcControlAim::lookAt(session(), pos, std::nullopt);
    QcPlayerInput intent = session().intent();
    intent.sneak = false;
    session().setIntent(intent);

    bool used = false;
    constexpr int openTicks = 10;

    m_context.begin({request, QcControlActiveCommand::timeoutPolls(openTicks),
                     [this, startTick, used, pos]() mutable {
                         if (session().machinePos() == pos) {
                             return true;
                         }

                         if (!used) {
                             if (session().tickCount() <= startTick) {
                                 return false;
                             }
                             if (!session().target() || session().target()->block != pos || !session().useItem()) {
                                 m_context.setActiveError(u"The block is not in reach or not in sight"_s);
                                 return true;
                             }
                             used = true;
                             return false;
                         }

                         if (session().tickCount() >= startTick + openTicks) {
                             m_context.setActiveError(u"The block did not open a machine"_s);
                             return true;
                         }

                         return false;
                     },
                     [this] { return m_context.reports().machineJson(); }});
}

void QcControlMachineCommands::fillMachine(const QJsonObject &request)
{
    // Moves the first inventory stack of the item into the machine through the normal slot moves.
    if (!session().machineOpen() || !session().content()) {
        m_context.fail(request, u"No machine is open"_s);
        return;
    }

    const QString into = request.value(u"into"_s).toString(u"input"_s);
    if (into != u"input"_s && into != u"fuel"_s) {
        m_context.fail(request, u"'into' must be input or fuel"_s);
        return;
    }

    const QcInventory &inventory = session().inventory();
    const std::optional<QcItemId> item = session().content()->itemId(request.value(u"item"_s).toString());
    int from = -1;
    for (int slot = 0; slot < QcInventory::slotCount && item; ++slot) {
        if (inventory.slot(slot).item == *item && !inventory.slot(slot).isEmpty()) {
            from = slot;
            break;
        }
    }

    if (from < 0) {
        m_context.fail(request, u"No such item in the inventory"_s);
        return;
    }

    moveMachineItem(request, from,
                    into == u"input"_s ? QcMsg::MoveInventoryItem::machineInputSlot : QcMsg::MoveInventoryItem::machineFuelSlot);
}

void QcControlMachineCommands::takeOutput(const QJsonObject &request)
{
    if (!session().machineOpen() || !session().content()) {
        m_context.fail(request, u"No machine is open"_s);
        return;
    }

    const QcItemStack output = session().machineSlots().value(2);
    if (output.isEmpty()) {
        m_context.fail(request, u"The output is empty"_s);
        return;
    }

    const int to = outputDestination(output);
    if (to < 0) {
        m_context.fail(request, u"No room in the inventory"_s);
        return;
    }

    moveMachineItem(request, QcMsg::MoveInventoryItem::machineOutputSlot, to);
}

int QcControlMachineCommands::outputDestination(QcItemStack output) const
{
    const auto content = session().content();
    const QcInventory &inventory = session().inventory();
    const int maxStack = content->isValidItem(output.item) ? content->item(output.item).maxStack : 1;

    for (int slot = 0; slot < QcInventory::slotCount; ++slot) {
        const QcItemStack &stack = inventory.slot(slot);
        if (stack.item == output.item && stack.damage == 0 && stack.count < maxStack) {
            return slot;
        }
    }

    for (int slot = 0; slot < QcInventory::slotCount; ++slot) {
        if (inventory.slot(slot).isEmpty()) {
            return slot;
        }
    }
    return -1;
}

void QcControlMachineCommands::moveMachineItem(const QJsonObject &request, int from, int to)
{
    const quint64 startTick = session().tickCount();
    session().moveInventoryItem(from, to, std::max(0, QcControlArguments::intField(request, u"count"_s, 0)));
    m_context.waitTicks(request, startTick, 3, [this] { return m_context.reports().machineJson(); });
}

void QcControlMachineCommands::closeMachine(const QJsonObject &request)
{
    session().closeMachine();
    m_context.finish(request, m_context.reports().machineJson());
}
