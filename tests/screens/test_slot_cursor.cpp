// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/inventory/QcSlotCursor.h"
#include "app/models/QcInventoryModel.h"
#include "common/content/registry/QcContentRegistry.h"
#include "fixtures/QcTestUtil.h"

#include <QSignalSpy>
#include <QTest>

#include <utility>

using namespace Qt::StringLiterals;

namespace {

// Records the requests the cursor makes, one line each, and locks a row on demand.
class RecordingActions : public QcSlotActions
{
public:
    int lockedRow() const override { return m_locked; }
    void moveItem(int from, int to, int count) const override { m_calls.append(u"move %1 %2 %3"_s.arg(from).arg(to).arg(count)); }
    void quickMove(int row) const override { m_calls.append(u"quick %1"_s.arg(row)); }
    void takeCraftingResult() const override { m_calls.append(u"result"_s); }
    void putCreativeItem(int row, const QString &itemName, int count) const override
    {
        m_calls.append(u"put %1 %2 %3"_s.arg(row).arg(itemName).arg(count));
    }
    void clearSlot(int row) const override { m_calls.append(u"clear %1"_s.arg(row)); }

    void setLocked(int row) { m_locked = row; }
    // The requests so far, forgotten once read.
    QStringList take() const { return std::exchange(m_calls, {}); }

private:
    int m_locked = -1;
    mutable QStringList m_calls;
};

} // namespace

// The click rules of the slot screens (QcSlotCursor, row 79.57): what a click on a slot, the
// creative catalogue or the bin asks of the server, and what stays in the hand.
class TestSlotCursor : public QObject
{
    Q_OBJECT

    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    QcInventoryModel m_model;
    RecordingActions m_actions;
    QcSlotCursor m_cursor{m_model, m_actions};

    static constexpr int dirtRow = 3;
    static constexpr int stickRow = 10;
    static constexpr int emptyRow = 20;
    static constexpr int machineInput = QcInventoryModel::machineFirstRow;
    static constexpr int machineOutput = QcInventoryModel::machineOutputRow;

    QcItemStack stackOf(const char *name, int count) const { return {*m_content->itemId(QString::fromLatin1(name)), count, 0}; }

    // Dirt and sticks in the inventory, a crafting result and a furnace with something done.
    void fill(bool withResult = true, bool withOutput = true)
    {
        QcInventory inventory;
        inventory.setSlot(dirtRow, stackOf("qtcraft:dirt", 10));
        inventory.setSlot(stickRow, stackOf("qtcraft:stick", 4));
        QList<QcItemStack> machine(3);
        if (withOutput) {
            machine[2] = stackOf("qtcraft:stick", 1);
        }
        m_model.setInventory(inventory, QList<QcItemStack>(9), withResult ? stackOf("qtcraft:stick", 4) : QcItemStack{}, machine);
    }

private slots:
    void init()
    {
        m_model.setContent(m_content, u"en"_s);
        fill();
        m_actions.setLocked(-1);
        m_cursor.showScreen(QcScreenState::Overlay::Inventory);
        m_actions.take();
    }

    void rowsMatchTheModel()
    {
        using Row = QcInventoryModel::Row;
        QCOMPARE(static_cast<int>(Row::HotbarEnd), 9);
        QCOMPARE(static_cast<int>(Row::InventoryEnd), 36);
        QCOMPARE(static_cast<int>(Row::GridFirst), 36);
        QCOMPARE(static_cast<int>(Row::Result), 45);
        QCOMPARE(static_cast<int>(Row::MachineFirst), 46);
        QCOMPARE(static_cast<int>(Row::MachineOutput), 48);
        QCOMPARE(static_cast<int>(Row::EquipmentFirst), QcInventoryModel::equipmentFirstRow);
        // The output row is the protocol's machine output slot.
        QCOMPARE(QcInventoryModel::slotOfRow(machineOutput), QcMsg::MoveInventoryItem::machineOutputSlot);
    }

    void leftClickPicksUpAndPutsDown()
    {
        QSignalSpy changed(&m_cursor, &QcSlotCursor::changed);
        m_cursor.click(dirtRow, Qt::LeftButton, false);
        QCOMPARE(m_cursor.pickedSlot(), dirtRow);
        QVERIFY(m_cursor.holding());
        QCOMPARE(changed.count(), 1);
        QVERIFY(m_actions.take().isEmpty());

        m_cursor.click(emptyRow, Qt::LeftButton, false);
        QCOMPARE(m_actions.take(), QStringList{u"move 3 20 0"_s});
        QCOMPARE(m_cursor.pickedSlot(), -1);
        QVERIFY(!m_cursor.holding());

        // Putting a stack back onto its own slot sends nothing.
        m_cursor.click(dirtRow, Qt::LeftButton, false);
        m_cursor.click(dirtRow, Qt::LeftButton, false);
        QVERIFY(m_actions.take().isEmpty());
        QCOMPARE(m_cursor.pickedSlot(), -1);
    }

    void anEmptySlotIsNotPickedUp()
    {
        m_cursor.click(emptyRow, Qt::LeftButton, false);
        m_cursor.click(emptyRow, Qt::LeftButton, true);
        QCOMPARE(m_cursor.pickedSlot(), -1);
        QVERIFY(m_actions.take().isEmpty());
    }

    void rightClickPutsOneAndKeepsTheStack()
    {
        m_cursor.click(dirtRow, Qt::LeftButton, false);
        m_cursor.click(emptyRow, Qt::RightButton, false);
        m_cursor.click(emptyRow + 1, Qt::RightButton, false);
        QCOMPARE(m_actions.take(), (QStringList{u"move 3 20 1"_s, u"move 3 21 1"_s}));
        QCOMPARE(m_cursor.pickedSlot(), dirtRow);
    }

    void shiftClickMovesAcross()
    {
        m_cursor.click(dirtRow, Qt::LeftButton, true);
        QCOMPARE(m_actions.take(), QStringList{u"quick 3"_s});
        QCOMPARE(m_cursor.pickedSlot(), -1);

        // With a stack picked up, a shift-click puts it down like a click.
        m_cursor.click(dirtRow, Qt::LeftButton, false);
        m_cursor.click(emptyRow, Qt::LeftButton, true);
        QCOMPARE(m_actions.take(), QStringList{u"move 3 20 0"_s});
    }

    void theResultIsTakenNotPickedUp()
    {
        m_cursor.click(QcInventoryModel::resultRow, Qt::LeftButton, false);
        m_cursor.click(QcInventoryModel::resultRow, Qt::LeftButton, true);
        QCOMPARE(m_actions.take(), (QStringList{u"result"_s, u"quick 45"_s}));
        QCOMPARE(m_cursor.pickedSlot(), -1);

        // Also with a stack picked up, which stays picked up.
        m_cursor.click(dirtRow, Qt::LeftButton, false);
        m_cursor.click(QcInventoryModel::resultRow, Qt::LeftButton, false);
        QCOMPARE(m_actions.take(), QStringList{u"result"_s});
        QCOMPARE(m_cursor.pickedSlot(), dirtRow);

        // Nothing to take: nothing is sent.
        fill(false);
        m_cursor.reset();
        m_cursor.click(QcInventoryModel::resultRow, Qt::LeftButton, false);
        QVERIFY(m_actions.take().isEmpty());
    }

    void nothingGoesIntoAMachineOutput()
    {
        m_cursor.showScreen(QcScreenState::Overlay::Furnace);
        QCOMPARE(m_cursor.outputRow(), machineOutput);
        m_cursor.click(dirtRow, Qt::LeftButton, false);
        m_cursor.click(machineOutput, Qt::RightButton, false);
        QVERIFY(m_actions.take().isEmpty());
        // Even a right click lets go of the stack there.
        QCOMPARE(m_cursor.pickedSlot(), -1);

        // The output is picked up like any slot and goes into the inventory.
        m_cursor.click(machineOutput, Qt::LeftButton, false);
        QCOMPARE(m_cursor.pickedSlot(), machineOutput);
        m_cursor.click(emptyRow, Qt::LeftButton, false);
        QCOMPARE(m_actions.take(), QStringList{u"move 48 20 0"_s});

        // The input takes stacks, and a chest's third slot is no output.
        m_cursor.click(dirtRow, Qt::LeftButton, false);
        m_cursor.click(machineInput, Qt::LeftButton, false);
        QCOMPARE(m_actions.take(), QStringList{u"move 3 46 0"_s});
        m_cursor.showScreen(QcScreenState::Overlay::Chest);
        QCOMPARE(m_cursor.outputRow(), -1);
        m_cursor.click(dirtRow, Qt::LeftButton, false);
        m_cursor.click(machineOutput, Qt::LeftButton, false);
        QCOMPARE(m_actions.take(), QStringList{u"move 3 48 0"_s});
        QCOMPARE(QcSlotCursor::outputRowOf(QcScreenState::Overlay::Stonecutter), machineOutput);
    }

    void theLockedSlotTakesNoClicks()
    {
        m_actions.setLocked(dirtRow);
        m_cursor.click(dirtRow, Qt::LeftButton, false);
        m_cursor.click(dirtRow, Qt::LeftButton, true);
        QCOMPARE(m_cursor.pickedSlot(), -1);
        m_cursor.click(stickRow, Qt::LeftButton, false);
        m_cursor.click(dirtRow, Qt::LeftButton, false);
        QVERIFY(m_actions.take().isEmpty());
        QCOMPARE(m_cursor.pickedSlot(), stickRow);
    }

    void aStackThatRanOutIsLetGo()
    {
        m_cursor.click(machineOutput, Qt::LeftButton, false);
        QCOMPARE(m_cursor.pickedSlot(), machineOutput);
        fill(true, false);
        QCOMPARE(m_cursor.pickedSlot(), -1);
    }

    void theCatalogueCarriesAndPuts()
    {
        m_cursor.takeFromCatalogue(u"qtcraft:dirt"_s, 64, Qt::LeftButton, false);
        QCOMPARE(m_cursor.carriedItem(), u"qtcraft:dirt"_s);
        QCOMPARE(m_cursor.carriedCount(), 64);
        QVERIFY(m_cursor.holding());

        m_cursor.click(emptyRow, Qt::LeftButton, false);
        QCOMPARE(m_actions.take(), QStringList{u"put 20 qtcraft:dirt 64"_s});
        QVERIFY(m_cursor.carriedItem().isEmpty());
        QCOMPARE(m_cursor.carriedCount(), 0);

        // Right click on the catalogue takes one; right clicks on slots put one each and keep it.
        m_cursor.takeFromCatalogue(u"qtcraft:stick"_s, 64, Qt::RightButton, false);
        QCOMPARE(m_cursor.carriedCount(), 1);
        m_cursor.click(emptyRow, Qt::RightButton, false);
        m_cursor.click(QcInventoryModel::resultRow, Qt::RightButton, false);
        QCOMPARE(m_actions.take(), QStringList{u"put 20 qtcraft:stick 1"_s});
        QCOMPARE(m_cursor.carriedItem(), u"qtcraft:stick"_s);

        // A left click on the result drops it without putting anything.
        m_cursor.click(QcInventoryModel::resultRow, Qt::LeftButton, false);
        QVERIFY(m_actions.take().isEmpty());
        QVERIFY(!m_cursor.holding());

        // Shift-click puts a full stack straight into the inventory.
        m_cursor.takeFromCatalogue(u"qtcraft:dirt"_s, 64, Qt::LeftButton, true);
        QCOMPARE(m_actions.take(), QStringList{u"put -1 qtcraft:dirt 64"_s});
        QVERIFY(!m_cursor.holding());
    }

    // A carried catalogue item goes nowhere into a machine's output: a right click keeps it, a
    // left click drops it there without a request.
    void aCarriedItemSkipsTheOutput()
    {
        m_cursor.showScreen(QcScreenState::Overlay::Furnace);
        m_cursor.takeFromCatalogue(u"qtcraft:dirt"_s, 64, Qt::LeftButton, false);
        m_cursor.click(machineOutput, Qt::RightButton, false);
        QVERIFY(m_actions.take().isEmpty());
        QCOMPARE(m_cursor.carriedItem(), u"qtcraft:dirt"_s);

        m_cursor.click(machineOutput, Qt::LeftButton, false);
        QVERIFY(m_actions.take().isEmpty());
        QVERIFY(!m_cursor.holding());
    }

    // The locked slot takes no carried item either, and the item stays carried.
    void aCarriedItemSkipsTheLockedSlot()
    {
        m_actions.setLocked(emptyRow);
        m_cursor.takeFromCatalogue(u"qtcraft:dirt"_s, 64, Qt::LeftButton, false);
        m_cursor.click(emptyRow, Qt::LeftButton, false);
        m_cursor.click(emptyRow, Qt::LeftButton, true);
        QVERIFY(m_actions.take().isEmpty());
        QCOMPARE(m_cursor.carriedCount(), 64);

        m_cursor.click(emptyRow + 1, Qt::LeftButton, false);
        QCOMPARE(m_actions.take(), QStringList{u"put 21 qtcraft:dirt 64"_s});
    }

    // Shift with a carried item puts it like a plain click (no quick move), the whole count on a
    // left click and one on a right click.
    void shiftWithACarriedItemPutsIt()
    {
        m_cursor.takeFromCatalogue(u"qtcraft:stick"_s, 64, Qt::RightButton, false);
        m_cursor.click(emptyRow, Qt::RightButton, true);
        QCOMPARE(m_actions.take(), QStringList{u"put 20 qtcraft:stick 1"_s});
        QVERIFY(m_cursor.holding());

        m_cursor.click(dirtRow, Qt::LeftButton, true);
        QCOMPARE(m_actions.take(), QStringList{u"put 3 qtcraft:stick 1"_s});
        QVERIFY(!m_cursor.holding());

        m_cursor.takeFromCatalogue(u"qtcraft:dirt"_s, 64, Qt::LeftButton, false);
        m_cursor.click(emptyRow, Qt::LeftButton, true);
        QCOMPARE(m_actions.take(), QStringList{u"put 20 qtcraft:dirt 64"_s});
        QVERIFY(!m_cursor.holding());
    }

    void theCatalogueAndTheBinDeleteAPickedStack()
    {
        m_cursor.click(dirtRow, Qt::LeftButton, false);
        m_cursor.takeFromCatalogue(u"qtcraft:stick"_s, 64, Qt::LeftButton, false);
        QCOMPARE(m_actions.take(), QStringList{u"clear 3"_s});
        QVERIFY(!m_cursor.holding());

        m_cursor.click(stickRow, Qt::LeftButton, false);
        m_cursor.bin();
        QCOMPARE(m_actions.take(), QStringList{u"clear 10"_s});
        QVERIFY(!m_cursor.holding());

        m_cursor.takeFromCatalogue(u"qtcraft:stick"_s, 64, Qt::LeftButton, false);
        m_cursor.bin();
        QVERIFY(m_actions.take().isEmpty());
        QVERIFY(!m_cursor.holding());
    }

    void anotherScreenEmptiesTheHand()
    {
        m_cursor.click(dirtRow, Qt::LeftButton, false);
        QSignalSpy changed(&m_cursor, &QcSlotCursor::changed);
        m_cursor.showScreen(QcScreenState::Overlay::None);
        QCOMPARE(m_cursor.pickedSlot(), -1);
        QCOMPARE(changed.count(), 1);
        // An empty hand stays quiet.
        m_cursor.reset();
        QCOMPARE(changed.count(), 1);

        m_cursor.takeFromCatalogue(u"qtcraft:dirt"_s, 64, Qt::LeftButton, false);
        m_cursor.reset();
        QVERIFY(!m_cursor.holding());
    }

    void badInputIsRefused()
    {
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"click on row -2 refused"_s));
        m_cursor.click(-2, Qt::LeftButton, false);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"pick of row 999 refused"_s));
        m_cursor.pick(999);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"catalogue item .* refused"_s));
        m_cursor.takeFromCatalogue({}, 64, Qt::LeftButton, false);
        QVERIFY(!m_cursor.holding());
        QVERIFY(m_actions.take().isEmpty());

        // The test hook picks a row up directly.
        m_cursor.pick(stickRow);
        QCOMPARE(m_cursor.pickedSlot(), stickRow);
    }
};

QTEST_GUILESS_MAIN(TestSlotCursor)
#include "test_slot_cursor.moc"
