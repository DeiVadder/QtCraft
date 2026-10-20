#pragma once

#include "common/gameplay/QcCrafting.h"
#include "common/gameplay/QcMining.h"
#include "common/physics/QcPhysics.h"
#include "QcCircuitFixture.h"

#include <QTest>

#include <cmath>
#include <utility>

// A circuit area with a player who holds items, turns and uses them like a client would (stage
// 62.2): the function blocks of package 62.2a are placed, used, dyed and mined through the
// protocol, so the tests see what a player sees.
class QcPlayerUseFixture : public QcCircuitFixture
{
    Q_OBJECT

protected:
    QcServerClient &player() { return *m_server->entities().clients().front(); }
    QcItemId baseItem(const QString &name) const { return item(qPrintable(QStringLiteral("qtcraft:") + name)); }

    // Gives the player `count` of an item and selects its hotbar slot.
    void hold(const QString &name, int count = 1)
    {
        chat(QStringLiteral("/give %1 %2").arg(name).arg(count));
        const int slot = slotOf(baseItem(name));
        QVERIFY(slot >= 0 && slot < QcInventory::hotbarSize);
        send(QcMsg::SelectHotbarSlot{slot});
    }

    // Turns the player to look along `yaw` (degrees; 0 north, 90 east).
    void face(float yaw)
    {
        QcPlayerInput input;
        input.sequence = ++m_sequence;
        input.yaw = yaw;
        send(QcMsg::PlayerInput{input});
    }

    void mine(QcBlockPos pos)
    {
        const QcBlockDefinition &block = m_content->block(*m_server->world().blockAt(pos));
        const QcItemStack &held = m_client->stacks[m_client->selected];
        const QcItemDefinition *tool = held.isEmpty() ? nullptr : &m_content->item(held.item);
        send(QcMsg::StartMining{pos});
        ticks(int(std::ceil(QcMining::breakSeconds(block, tool) * QcPhysics::tickRate)) + 1);
        send(QcMsg::FinishMining{pos});
        QCOMPARE(*m_server->world().blockAt(pos), qcAirBlockId);
    }

    // The item and count a crafting grid of `size` x `size` with one item in each listed cell
    // makes (empty when no recipe matches).
    std::pair<QString, int> crafted(const QList<std::pair<int, QString>> &cells, int size) const
    {
        QList<QcItemStack> grid(qsizetype(size) * size);
        for (const auto &[cell, name] : cells) {
            grid[cell] = {baseItem(name), 1, 0};
        }
        const std::optional<qsizetype> recipe = QcCrafting::match(*m_content, grid, size);
        if (!recipe) {
            return {};
        }
        return {m_content->recipes()[*recipe].resultItem, m_content->recipes()[*recipe].resultCount};
    }

    QString propertyAt(QcBlockPos pos, const QString &name) const { return m_content->property(*m_server->world().blockAt(pos), name); }
    QString blockNameAt(QcBlockPos pos) const { return m_content->block(*m_server->world().blockAt(pos)).name; }
};
