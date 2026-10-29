// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/input/QcGamepadMenuInput.h"
#include "clientcore/input/QcHotbarRadial.h"
#include "clientcore/input/QcSpatialNavigation.h"
#include "clientcore/input/QcVirtualGamepadSource.h"
#include "clientcore/player/QcClientInventory.h"

#include <QSignalSpy>
#include <QTest>

#include <limits>

using namespace Qt::StringLiterals;
using Command = QcGamepadMenuInput::Command;
using Direction = QcSpatialNavigation::Direction;

namespace {

// A grid of `columns` x `rows` cells of 50 x 50 with gaps of 4, from the top left.
QList<QRectF> grid(int columns, int rows)
{
    QList<QRectF> cells;
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            cells.append(QRectF(column * 54.0, row * 54.0, 50.0, 50.0));
        }
    }
    return cells;
}

QcMsg::InventoryState stateSelecting(int slot)
{
    QcMsg::InventoryState state;
    state.stacks = QList<QcItemStack>(QcInventory::slotCount);
    state.selected = slot;
    return state;
}

} // namespace

// The pad's parts on the screens without a window (decisions E-581 to E-583): moving a focus by
// direction between rectangles, the hotbar's radial menu, the menu commands of a pad with their
// repeats, and the hotbar selection that an older answer of the server does not take back (E-582).
class TestGamepadNavigation : public QObject
{
    Q_OBJECT

private:
    QcVirtualGamepadSource m_source;
    int m_pad = 0;

private slots:
    void init() { m_pad = m_source.connectPad(u"pad"_s, QcGamepadType::Xbox); }
    void cleanup() { m_source.disconnectPad(m_pad); }

    // In a grid the neighbours in the row and the column win over nearer diagonal cells; at the
    // edge nothing lies that way.
    void focusMovesToTheNeighbourInAGrid()
    {
        const QList<QRectF> cells = grid(3, 3);
        QCOMPARE(QcSpatialNavigation::next(cells[4], Direction::Right, cells), 5);
        QCOMPARE(QcSpatialNavigation::next(cells[4], Direction::Left, cells), 3);
        QCOMPARE(QcSpatialNavigation::next(cells[4], Direction::Up, cells), 1);
        QCOMPARE(QcSpatialNavigation::next(cells[4], Direction::Down, cells), 7);
        QCOMPARE(QcSpatialNavigation::next(cells[2], Direction::Right, cells), std::nullopt);
        QCOMPARE(QcSpatialNavigation::next(cells[0], Direction::Up, cells), std::nullopt);
        QCOMPARE(QcSpatialNavigation::next(cells[0], Direction::Up, {}), std::nullopt);
    }

    // A column of wide buttons beside a grid: right from a button reaches the grid's cell in its
    // row; left from any cell of the first column comes back to the button beside it; an item at
    // the same place never counts.
    void focusCrossesBetweenAColumnAndAGrid()
    {
        QList<QRectF> items{QRectF(0, 0, 200, 40), QRectF(0, 60, 200, 40), QRectF(0, 120, 200, 40)};
        const QList<QRectF> cells = grid(2, 3);
        for (const QRectF &cell : cells) {
            items.append(cell.translated(260, 0));
        }

        // The middle button (y 60..100) overlaps the second row of cells (54..104).
        QCOMPARE(QcSpatialNavigation::next(items[1], Direction::Right, items), 5);
        QCOMPARE(QcSpatialNavigation::next(items[7], Direction::Left, items), 2);
        QCOMPARE(QcSpatialNavigation::next(items[0], Direction::Down, items), 1);
        // Far off to the side is not "down": the end of the column stays.
        QCOMPARE(QcSpatialNavigation::next(items[2], Direction::Down, items), std::nullopt);
        QList<QRectF> twins{QRectF(0, 0, 10, 10), QRectF(0, 0, 10, 10)};
        QCOMPARE(QcSpatialNavigation::next(twins[0], Direction::Right, twins), std::nullopt);
        QCOMPARE(QcSpatialNavigation::next(twins[0], Direction::Down, twins), std::nullopt);
    }

    void focusStartsTopLeft()
    {
        QCOMPARE(QcSpatialNavigation::first({}), std::nullopt);
        // Rows by centre with a tolerance: a slightly higher cell further right is in the same row.
        const QList<QRectF> items{QRectF(300, 102, 50, 50), QRectF(100, 104, 50, 50), QRectF(200, 100, 50, 50), QRectF(0, 300, 50, 50)};
        QCOMPARE(QcSpatialNavigation::first(items), 1);
        QCOMPARE(QcSpatialNavigation::first(grid(4, 4)), 0);
    }

    // Slot 0 at the top, clockwise; a stick near the centre points at nothing.
    void radialSectorsGoClockwiseFromTheTop()
    {
        QCOMPARE(QcHotbarRadial::slotAt(0.0f, -1.0f, 9), 0);
        QCOMPARE(QcHotbarRadial::slotAt(1.0f, 0.0f, 9), 2);
        QCOMPARE(QcHotbarRadial::slotAt(0.0f, 1.0f, 9), 5);
        QCOMPARE(QcHotbarRadial::slotAt(-1.0f, 0.0f, 9), 7);
        // Just left of straight up is still slot 0 (sectors are centred on their slot).
        QCOMPARE(QcHotbarRadial::slotAt(-0.2f, -1.0f, 9), 0);
        QCOMPARE(QcHotbarRadial::slotAt(-0.5f, -0.6f, 9), 8);
        QCOMPARE(QcHotbarRadial::slotAt(0.3f, 0.3f, 9), std::nullopt);
        QCOMPARE(QcHotbarRadial::slotAt(std::numeric_limits<float>::quiet_NaN(), 1.0f, 9), std::nullopt);
    }

    // It opens on the slot in hand, keeps the last slot pointed at when the stick comes back and
    // gives it when closed; cancelling gives nothing.
    void radialKeepsTheLastSlotPointedAt()
    {
        QcHotbarRadial radial(9);
        QCOMPARE(radial.highlighted(), -1);
        QVERIFY(!radial.point(1.0f, 0.0f));
        radial.open(4);
        QCOMPARE(radial.highlighted(), 4);
        QVERIFY(radial.point(1.0f, 0.0f));
        QVERIFY(!radial.point(1.0f, 0.0f));
        QVERIFY(!radial.point(0.0f, 0.0f));
        QCOMPARE(radial.highlighted(), 2);
        QCOMPARE(radial.close(), 2);
        QCOMPARE(radial.close(), std::nullopt);

        radial.open(12);
        QCOMPARE(radial.highlighted(), 8);
        radial.cancel();
        QVERIFY(!radial.isOpen());
        QCOMPARE(radial.close(), std::nullopt);
    }

    // Buttons give their command once per press; the d-pad and the left stick move.
    void buttonsGiveCommands()
    {
        QcGamepadMenuInput menu;
        menu.setSource(&m_source);
        QSignalSpy commands(&menu, &QcGamepadMenuInput::command);
        const QList<std::pair<QcGamepadButton, Command>> buttons{
            {QcGamepadButton::South, Command::Confirm},      {QcGamepadButton::East, Command::Back},
            {QcGamepadButton::West, Command::Secondary},     {QcGamepadButton::North, Command::Tertiary},
            {QcGamepadButton::Start, Command::Start},        {QcGamepadButton::LeftShoulder, Command::TabPrevious},
            {QcGamepadButton::RightShoulder, Command::TabNext}, {QcGamepadButton::DpadRight, Command::Right},
        };
        for (const auto &[button, command] : buttons) {
            commands.clear();
            m_source.setPressed(m_pad, button, true);
            m_source.setPressed(m_pad, button, false);
            QCOMPARE(commands.size(), 1);
            QCOMPARE(commands.first().first().value<Command>(), command);
        }

        commands.clear();
        m_source.setAxis(m_pad, QcGamepadAxis::LeftY, 0.3f);
        QCOMPARE(commands.size(), 0);
        m_source.setAxis(m_pad, QcGamepadAxis::LeftY, 0.8f);
        m_source.setAxis(m_pad, QcGamepadAxis::LeftX, 0.2f);
        QCOMPARE(commands.size(), 1);
        QCOMPARE(commands.first().first().value<Command>(), Command::Down);
        // Turning the stick to the left is a new direction.
        m_source.setAxis(m_pad, QcGamepadAxis::LeftX, -0.9f);
        QCOMPARE(commands.size(), 2);
        QCOMPARE(commands.last().first().value<Command>(), Command::Left);
        m_source.setAxis(m_pad, QcGamepadAxis::LeftX, 0.0f);
        m_source.setAxis(m_pad, QcGamepadAxis::LeftY, 0.0f);
    }

    // A held direction repeats after the delay at the interval, and stops when let go.
    void heldDirectionRepeats()
    {
        QcGamepadMenuInput menu;
        menu.setSource(&m_source);
        QSignalSpy commands(&menu, &QcGamepadMenuInput::command);
        m_source.setPressed(m_pad, QcGamepadButton::DpadDown, true);
        const qint64 start = menu.now();
        QCOMPARE(commands.size(), 1);

        menu.advance(start + QcGamepadMenuInput::repeatDelayMs - 30);
        QCOMPARE(commands.size(), 1);
        menu.advance(start + QcGamepadMenuInput::repeatDelayMs + 30);
        QCOMPARE(commands.size(), 2);
        menu.advance(start + QcGamepadMenuInput::repeatDelayMs + 3 * QcGamepadMenuInput::repeatIntervalMs + 30);
        QCOMPARE(commands.size(), 5);
        for (const QList<QVariant> &command : std::as_const(commands)) {
            QCOMPARE(command.first().value<Command>(), Command::Down);
        }

        m_source.setPressed(m_pad, QcGamepadButton::DpadDown, false);
        menu.advance(start + 10000);
        QCOMPARE(commands.size(), 5);
    }

    // The right stick scrolls by its deflection and the time passed; inside its dead zone it does not.
    void rightStickScrolls()
    {
        QcGamepadMenuInput menu;
        menu.setSource(&m_source);
        QSignalSpy scrolled(&menu, &QcGamepadMenuInput::scrolled);
        m_source.setAxis(m_pad, QcGamepadAxis::RightY, 0.2f);
        menu.advance(menu.now() + 50);
        QCOMPARE(scrolled.size(), 0);

        m_source.setAxis(m_pad, QcGamepadAxis::RightY, -1.0f);
        const qint64 start = menu.now();
        menu.advance(start);
        menu.advance(start + 50);
        QVERIFY(!scrolled.isEmpty());
        const float pixels = scrolled.last().first().toFloat();
        QVERIFY2(pixels < -0.04f * QcGamepadMenuInput::scrollSpeed, qPrintable(QString::number(pixels)));
        m_source.setAxis(m_pad, QcGamepadAxis::RightY, 0.0f);
    }

    // The hotbar flicker (decision E-582): a state the server sent before it saw the newer
    // selections carries an older selection and does not take back the local one; the answer to
    // the last selection counts again, and a selection left unanswered is given up in time.
    void olderServerStateKeepsTheLocalSelection()
    {
        QList<QcMessage> sent;
        QcClientInventory inventory([&sent](const QcMessage &message) { sent.append(message); });
        QVERIFY(inventory.apply(stateSelecting(0), nullptr));
        inventory.selectHotbarSlot(1);
        inventory.selectHotbarSlot(2);
        QCOMPARE(sent.size(), 2);
        QCOMPARE(inventory.pendingSelections(), 2);

        // An item picked up before the server read the selections.
        QVERIFY(inventory.apply(stateSelecting(0), nullptr));
        QCOMPARE(inventory.inventory().selectedHotbarSlot(), 2);
        // The answer to the first selection: the second is still on its way.
        QVERIFY(inventory.apply(stateSelecting(1), nullptr));
        QCOMPARE(inventory.inventory().selectedHotbarSlot(), 2);
        QVERIFY(inventory.apply(stateSelecting(2), nullptr));
        QCOMPARE(inventory.pendingSelections(), 0);

        // The server's own change (nothing on its way) is taken.
        QVERIFY(inventory.apply(stateSelecting(6), nullptr));
        QCOMPARE(inventory.inventory().selectedHotbarSlot(), 6);
    }

    // A selection the server dropped (rate limit) is sent again once it waited pendingSelectionMs,
    // with a state or with the session's tick, and the local selection stays.
    void lostSelectionIsSentAgain()
    {
        QList<QcMessage> sent;
        qint64 now = 0;
        QcClientInventory inventory([&sent](const QcMessage &message) { sent.append(message); });
        inventory.setClock([&now] { return now; });
        inventory.selectHotbarSlot(3);
        QVERIFY(inventory.apply(stateSelecting(0), nullptr));
        QCOMPARE(inventory.inventory().selectedHotbarSlot(), 3);

        now = QcClientInventory::pendingSelectionMs;
        inventory.resendLostSelection();
        QCOMPARE(sent.size(), 1);
        now += 1;
        QVERIFY(inventory.apply(stateSelecting(0), nullptr));
        QCOMPARE(inventory.inventory().selectedHotbarSlot(), 3);
        QCOMPARE(sent.size(), 2);
        QCOMPARE(std::get<QcMsg::SelectHotbarSlot>(sent.last()).slot, 3);
        QCOMPARE(inventory.pendingSelections(), 1);

        // Dropped again: the session's tick sends it once more; then the answer arrives.
        now += QcClientInventory::pendingSelectionMs + 1;
        inventory.resendLostSelection();
        QCOMPARE(sent.size(), 3);
        inventory.resendLostSelection();
        QCOMPARE(sent.size(), 3);
        QVERIFY(inventory.apply(stateSelecting(3), nullptr));
        QCOMPARE(inventory.pendingSelections(), 0);
        QVERIFY(inventory.apply(stateSelecting(5), nullptr));
        QCOMPARE(inventory.inventory().selectedHotbarSlot(), 5);
    }
};

QTEST_GUILESS_MAIN(TestGamepadNavigation)
#include "test_gamepad_navigation.moc"
