#include "common/content/registry/QcContentRegistry.h"
#include "common/gameplay/QcInventory.h"
#include "common/gameplay/QcQuickMove.h"
#include "common/gameplay/QcSlotList.h"
#include "QcServerTestFixture.h"

#include <QTest>

using namespace Qt::StringLiterals;

namespace {

constexpr int grid = QcMsg::MoveInventoryItem::craftingGridSlot;

QList<int> range(int first, int count)
{
    QList<int> result;
    for (int i = 0; i < count; ++i) {
        result.append(first + i);
    }
    return result;
}

} // namespace

// Shift-click (QcMsg::QuickMoveItem): where stacks go, decided by the server.
class TestQuickMove : public QcServerTestFixture
{
    Q_OBJECT

    void quickMove(int slot) { send(QcMsg::QuickMoveItem{slot}); }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void targetsFollowTheOpenScreen()
    {
        using QcQuickMove::targets;
        const QcQuickMove::Situation inventory;
        QCOMPARE(targets(3, inventory), range(9, 27));
        QCOMPARE(targets(20, inventory), range(0, 9));
        QCOMPARE(targets(grid + 4, inventory), range(0, 36));

        QcQuickMove::Situation chest;
        chest.containerSlots = 27;
        QCOMPARE(targets(3, chest), range(inputSlot, 27));
        QCOMPARE(targets(inputSlot + 26, chest), range(0, 36));

        QcQuickMove::Situation furnace;
        furnace.machineOpen = true;
        QVERIFY(targets(3, furnace).isEmpty());
        furnace.fuel = true;
        QCOMPARE(targets(3, furnace), QList<int>{fuelSlot});
        furnace.smeltable = true;
        QCOMPARE(targets(3, furnace), (QList<int>{inputSlot, fuelSlot}));
        QCOMPARE(targets(inputSlot + 2, furnace), range(0, 36));
        QVERIFY(targets(-1, inventory).isEmpty());
    }

    void moveIntoFillsEqualStacksFirst()
    {
        join();
        const QcItemId dirt = item("qtcraft:dirt");
        const QcItemId stone = item("qtcraft:stone");
        QcSlotList slotList{QList<QcItemStack>(6)};
        QList<QcItemStack> &stacks = slotList.stacks;
        stacks[0] = {dirt, 40, 0};
        stacks[1] = {stone, 3, 0};
        stacks[3] = {dirt, 60, 0};
        stacks[5] = {dirt, 10, 0};
        // 60 + 4 fills slot 3 to 64 first although slot 2 is empty and comes earlier.
        QCOMPARE(slotList.moveInto(0, {1, 2, 3, 5}, *m_content), 40);
        QCOMPARE(stacks[3].count, 64);
        QCOMPARE(stacks[5].count, 46);
        QVERIFY(stacks[0].isEmpty());
        QVERIFY(stacks[2].isEmpty());
        QCOMPARE(stacks[1], (QcItemStack{stone, 3, 0}));

        // Without room nothing moves.
        QCOMPARE(slotList.moveInto(1, {3}, *m_content), 0);
        QCOMPARE(stacks[1].count, 3);
    }

    void shiftClickMovesBetweenHotbarAndStorage()
    {
        join();
        chat(u"/give dirt 10"_s);
        const int hotbarSlot = slotOf(item("qtcraft:dirt"));
        QVERIFY(hotbarSlot < 9);
        quickMove(hotbarSlot);
        QVERIFY(m_client->stacks[hotbarSlot].isEmpty());
        const int storageSlot = slotOf(item("qtcraft:dirt"));
        QCOMPARE(storageSlot, 9);
        QCOMPARE(countOf(item("qtcraft:dirt")), 10);

        quickMove(storageSlot);
        QVERIFY(slotOf(item("qtcraft:dirt")) < 9);
        QCOMPARE(countOf(item("qtcraft:dirt")), 10);

        // An empty slot, a slot out of range or of a block that is not open changes nothing.
        quickMove(30);
        quickMove(inputSlot);
        QCOMPARE(countOf(item("qtcraft:dirt")), 10);
    }

    void shiftClickEmptiesTheGridAndCraftsAll()
    {
        join();
        chat(u"/give oak_log 5"_s);
        send(QcMsg::MoveInventoryItem{slotOf(item("qtcraft:oak_log")), grid, 3});
        QCOMPARE(m_client->craftingGrid[0].count, 3);
        quickMove(grid);
        QVERIFY(m_client->craftingGrid[0].isEmpty());
        QCOMPARE(countOf(item("qtcraft:oak_log")), 5);

        // Three logs in the grid become twelve planks in one shift-click.
        send(QcMsg::MoveInventoryItem{slotOf(item("qtcraft:oak_log")), grid, 3});
        quickMove(QcMsg::QuickMoveItem::craftingResultSlot);
        QCOMPARE(countOf(item("qtcraft:oak_planks")), 12);
        QVERIFY(m_client->craftingGrid[0].isEmpty());
        QCOMPARE(countOf(item("qtcraft:oak_log")), 2);
        QVERIFY(m_client->craftingResult.isEmpty());
    }

    void shiftClickFillsTheFurnaceByItemKind()
    {
        join();
        const QcBlockPos pos = openFurnace();
        QCOMPARE(m_client->openMachine, std::optional<QcBlockPos>(pos));
        chat(u"/give raw_iron 3"_s);
        chat(u"/give coal 2"_s);
        chat(u"/give dirt 2"_s);
        // Ore into the input, coal onto the fuel, dirt nowhere.
        quickMove(slotOf(item("qtcraft:raw_iron")));
        quickMove(slotOf(item("qtcraft:coal")));
        quickMove(slotOf(item("qtcraft:dirt")));
        QCOMPARE(m_client->machineStacks[0], (QcItemStack{item("qtcraft:raw_iron"), 3, 0}));
        // The furnace lit at once and burns one coal.
        QCOMPARE(m_client->machineStacks[1], (QcItemStack{item("qtcraft:coal"), 1, 0}));
        QCOMPARE(countOf(item("qtcraft:coal")), 0);
        QCOMPARE(countOf(item("qtcraft:dirt")), 2);

        // And back out into the inventory.
        quickMove(fuelSlot);
        QCOMPARE(countOf(item("qtcraft:coal")), 1);
        QVERIFY(m_client->machineStacks[1].isEmpty());
    }

    void shiftClickFillsAnOpenChest()
    {
        join();
        chat(u"/give chest"_s);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:chest"))});
        const QcBlockPos pos = m_feet.offset(2, 0, 0);
        send(QcMsg::UseItem{pos.offset(0, -1, 0), QcBlockFace::Up});
        send(QcMsg::UseItem{pos, QcBlockFace::West});
        QCOMPARE(m_client->openMachine, std::optional<QcBlockPos>(pos));
        chat(u"/give stone 70"_s);

        quickMove(slotOf(item("qtcraft:stone")));
        quickMove(slotOf(item("qtcraft:stone")));
        QCOMPARE(countOf(item("qtcraft:stone")), 0);
        QCOMPARE(m_client->machineStacks[0], (QcItemStack{item("qtcraft:stone"), 64, 0}));
        QCOMPARE(m_client->machineStacks[1], (QcItemStack{item("qtcraft:stone"), 6, 0}));

        quickMove(inputSlot + 1);
        QCOMPARE(countOf(item("qtcraft:stone")), 6);
        QVERIFY(m_client->machineStacks[1].isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestQuickMove)
#include "test_quick_move.moc"
