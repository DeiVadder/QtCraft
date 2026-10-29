// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/input/QcFocusCandidates.h"
#include "app/input/QcGamepadUi.h"
#include "clientcore/input/QcGamepadControls.h"
#include "clientcore/input/QcVirtualGamepadSource.h"

#include <QElapsedTimer>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

// A screen like the game's: a column of buttons (the middle one the default), a grid of slots
// that count their clicks, a text field, a tab bar, a long list, a slider, a spin box and a popup;
// Escape counts on the root, which keeps the keys like the game's screens do. `extraSlots` fills
// a page of slots below (the size of the creative catalogue) for the measurement.
const char *const sceneQml = R"(
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    width: 900
    height: 600
    visible: true

    Item {
        id: screen
        objectName: "screen"
        anchors.fill: parent
        focus: true
        property int escapes: 0
        property string lastClick
        property bool takeTabs: false
        property int extraSlots: 0
        property bool sideways: false
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Escape)
                escapes++;
            event.accepted = true;
        }
        function gamepadCommand(command: string): bool {
            if (takeTabs && command === "tabNext") {
                lastClick = "handler";
                return true;
            }
            return false;
        }

        Column {
            id: column
            x: 20; y: 20; spacing: 10
            Button { objectName: "first"; width: 200; text: "First"; onClicked: screen.lastClick = "first" }
            Button { objectName: "middle"; width: 200; text: "Middle"; highlighted: true; onClicked: screen.lastClick = "middle" }
            Button { objectName: "last"; width: 200; text: "Last"; onClicked: screen.lastClick = "last"; enabled: true }
            Button { objectName: "hidden"; width: 200; text: "Hidden"; visible: false }
            Button { objectName: "disabled"; width: 200; text: "Disabled"; enabled: false }
            TextField { objectName: "name"; width: 200 }
            Slider { id: slider; objectName: "slider"; width: 200; from: 0; to: 10; value: 5; onMoved: screen.lastClick = "slider" }
            Button { objectName: "openPopup"; width: 200; text: "Popup"; onClicked: popup.open() }
        }

        Grid {
            x: 300; y: 20; columns: 3; spacing: 4
            Repeater {
                model: 9
                Rectangle {
                    required property int index
                    objectName: "cell" + index
                    width: 50; height: 50
                    activeFocusOnTab: true
                    color: "grey"
                    // As in the game's slots: one handler without and one with shift.
                    TapHandler {
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        acceptedModifiers: Qt.NoModifier
                        onTapped: (point, button) => screen.lastClick = parent.objectName
                            + (button === Qt.RightButton ? ":right" : ":left")
                    }
                    TapHandler {
                        acceptedModifiers: Qt.ShiftModifier
                        onTapped: screen.lastClick = parent.objectName + ":left:shift"
                    }
                }
            }
        }

        SpinBox {
            objectName: "spin"
            // Right of the column, between the slider and the button below it (reached from that button).
            x: 300; y: column.y + slider.y - 5; width: 150
            from: 1; to: 64; value: 8
            editable: true
            onValueModified: screen.lastClick = "spin"
        }

        Flickable {
            objectName: "page"
            x: 20; y: 380; width: 860; height: 200
            visible: screen.extraSlots > 0
            clip: true
            contentHeight: slotGrid.height
            Grid {
                id: slotGrid
                columns: 20; spacing: 4
                Repeater {
                    model: screen.extraSlots
                    Rectangle { width: 36; height: 36; activeFocusOnTab: true; color: "grey" }
                }
            }
        }

        // A wide row that scrolls sideways and a strip that snaps to its cells (sideways only).
        Flickable {
            objectName: "wide"
            x: 20; y: 400; width: 400; height: 60
            visible: screen.sideways
            clip: true
            contentWidth: wideRow.width
            Row {
                id: wideRow
                spacing: 10
                Repeater {
                    model: 20
                    Button { required property int index; objectName: "wide" + index; width: 80; text: "W" + index }
                }
            }
        }
        ListView {
            objectName: "strip"
            x: 450; y: 400; width: 400; height: 60
            visible: screen.sideways
            orientation: ListView.Horizontal
            snapMode: ListView.SnapToItem
            clip: true
            model: 20
            delegate: ItemDelegate { required property int index; objectName: "strip" + index; width: 80; height: 60; text: "S" + index }
        }

        TabBar {
            objectName: "tabs"
            x: 500; y: 20; width: 300
            TabButton { text: "One" }
            TabButton { text: "Two" }
            TabButton { text: "Three" }
        }

        ListView {
            objectName: "list"
            x: 500; y: 100; width: 300; height: 200
            clip: true
            model: 40
            delegate: ItemDelegate {
                required property int index
                objectName: "row" + index
                width: 300; height: 40
                text: "Row " + index
            }
        }

        Popup {
            id: popup
            objectName: "popup"
            x: 300; y: 300; width: 200; height: 150
            modal: true
            focus: true
            Column {
                Button { objectName: "popupYes"; text: "Yes"; onClicked: { screen.lastClick = "popupYes"; popup.close(); } }
                Button { objectName: "popupNo"; text: "No" }
            }
        }
    }
}
)";

} // namespace

// The pad on screens of a real window (decisions E-583, E-584): a virtual pad drives
// QcGamepadMenuInput, QcGamepadUi and QcMenuNavigator over a scene built like the game's screens.
// The focus starts on the default button, moves by direction across a column, a grid and a list
// (scrolling it), skips hidden and disabled items, clicks with A, right-clicks with X and
// shift-clicks with Y, goes back with Escape, switches tabs, asks the screen's gamepadCommand()
// first, opens the keyboard for text fields, stays inside an open popup, and the ring follows the
// focus only while the pad was used last; without the pad in use none of it runs per frame. A back
// entry (padLeftOnly) is reached only by left or by up from within its group.
class TestGamepadScreens : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QQuickWindow *m_window = nullptr;
    QcVirtualGamepadSource m_source;
    QcGamepadControls m_world;
    std::unique_ptr<QcGamepadUi> m_ui;
    int m_pad = 0;
    // The gate: whether the screens take the pad (the world does not).
    bool m_screensOpen = true;

    QQuickItem *item(const QString &name) const { return m_window->findChild<QQuickItem *>(name); }

    // An item below `root` by its items (a Repeater's delegates outside a view have no QObject parent).
    static QQuickItem *itemBelow(QQuickItem *root, const QString &name)
    {
        const QList<QQuickItem *> children = root->childItems();
        for (QQuickItem *child : children) {
            QQuickItem *found = child->objectName() == name ? child : itemBelow(child, name);
            if (found) {
                return found;
            }
        }
        return nullptr;
    }
    QObject *screen() const { return item(u"screen"_s); }
    QString focus() const { return m_ui->focusDescription(); }

    void click(QcGamepadButton button)
    {
        m_source.setPressed(m_pad, button, true);
        m_source.setPressed(m_pad, button, false);
        QCoreApplication::processEvents();
    }

    // Moves `steps` times in the d-pad direction `button`.
    void move(QcGamepadButton button, int steps = 1)
    {
        for (int step = 0; step < steps; ++step) {
            click(button);
        }
    }

private slots:
    // The game's style on every system (decision E-845, QcAppWindow::applyControlStyle); the macOS
    // style would draw through AppKit, which the platform without windows does not have.
    void initTestCase() { QQuickStyle::setStyle(u"Basic"_s); }

    void init()
    {
        m_engine = std::make_unique<QQmlApplicationEngine>();
        m_engine->loadData(sceneQml);
        QVERIFY(!m_engine->rootObjects().isEmpty());
        m_window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().first());
        QVERIFY(m_window);
        QVERIFY(QTest::qWaitForWindowExposed(m_window));

        m_pad = m_source.connectPad(u"pad"_s, QcGamepadType::Xbox);
        m_screensOpen = true;
        m_ui = std::make_unique<QcGamepadUi>(m_world, [this] { return m_screensOpen; });
        m_ui->setSource(&m_source);
        m_ui->setWindow(m_window);
        m_ui->setIgnoreWindowFocus(true);
    }

    void cleanup()
    {
        m_ui.reset();
        m_source.disconnectPad(m_pad);
        m_engine.reset();
        m_window = nullptr;
    }

    // The first press shows the focus on the default button; then it moves down the column,
    // skipping hidden and disabled buttons, and stops at the end.
    void focusStartsOnTheDefaultAndMovesDown()
    {
        QVERIFY(!m_ui->focusShown());
        click(QcGamepadButton::DpadDown);
        QVERIFY(m_ui->active());
        QCOMPARE(focus(), u"middle"_s);
        QVERIFY(m_ui->focusShown());
        QCOMPARE(m_ui->focusRect(), item(u"middle"_s)->mapRectToScene(QRectF(0, 0, 200, item(u"middle"_s)->height())));

        move(QcGamepadButton::DpadDown);
        QCOMPARE(focus(), u"last"_s);
        move(QcGamepadButton::DpadDown);
        QCOMPARE(focus(), u"name"_s);
        move(QcGamepadButton::DpadDown, 2);
        QCOMPARE(focus(), u"openPopup"_s);
        move(QcGamepadButton::DpadDown);
        QCOMPARE(focus(), u"openPopup"_s);
        move(QcGamepadButton::DpadUp, 5);
        QCOMPARE(focus(), u"first"_s);
    }

    // An item marked padLeftOnly (a side bar's back entry, docs/UI-Leitfaden.md 10, decision E-925)
    // is skipped by down, reached by up from within its own group and by left, and never where the
    // focus starts again, not even as the focus remembered last.
    void leftOnlyItemIsReachedOnlyByUpInItsGroupOrLeft()
    {
        item(u"last"_s)->setProperty("padLeftOnly", true);
        click(QcGamepadButton::DpadDown);
        QCOMPARE(focus(), u"middle"_s);

        move(QcGamepadButton::DpadDown);
        QCOMPARE(focus(), u"name"_s);
        move(QcGamepadButton::DpadUp);
        QCOMPARE(focus(), u"last"_s);

        item(u"screen"_s)->forceActiveFocus();
        QTRY_VERIFY(!focus().isEmpty());
        QVERIFY(focus() != u"last"_s);
        m_ui->navigator().focusStart();
        QVERIFY(focus() != u"last"_s);

        move(QcGamepadButton::DpadUp, 3);
        QCOMPARE(focus(), u"first"_s);
        move(QcGamepadButton::DpadRight);
        QCOMPARE(focus(), u"cell0"_s);
        item(u"first"_s)->setProperty("padLeftOnly", true);
        move(QcGamepadButton::DpadLeft);
        QCOMPARE(focus(), u"first"_s);
    }

    // Right from the column reaches the grid in the same row; A clicks, X right-clicks, Y
    // shift-clicks the focused slot.
    void slotsTakeClicksFromThePad()
    {
        click(QcGamepadButton::DpadDown);
        move(QcGamepadButton::DpadUp);
        QCOMPARE(focus(), u"first"_s);
        move(QcGamepadButton::DpadRight);
        QCOMPARE(focus(), u"cell0"_s);
        move(QcGamepadButton::DpadDown);
        move(QcGamepadButton::DpadRight);
        QCOMPARE(focus(), u"cell4"_s);

        click(QcGamepadButton::South);
        QCOMPARE(screen()->property("lastClick").toString(), u"cell4:left"_s);
        click(QcGamepadButton::West);
        QCOMPARE(screen()->property("lastClick").toString(), u"cell4:right"_s);
        click(QcGamepadButton::North);
        QCOMPARE(screen()->property("lastClick").toString(), u"cell4:left:shift"_s);

        move(QcGamepadButton::DpadLeft, 2);
        QCOMPARE(focus(), u"middle"_s);
        click(QcGamepadButton::South);
        QCOMPARE(screen()->property("lastClick").toString(), u"middle"_s);
    }

    // B and start go back as Escape does; the shoulders switch the tab bar's tab unless the
    // screen's gamepadCommand() takes them.
    void backAndTabs()
    {
        click(QcGamepadButton::East);
        QCOMPARE(screen()->property("escapes").toInt(), 1);
        click(QcGamepadButton::Start);
        QCOMPARE(screen()->property("escapes").toInt(), 2);

        QObject *tabs = item(u"tabs"_s);
        click(QcGamepadButton::RightShoulder);
        QCOMPARE(tabs->property("currentIndex").toInt(), 1);
        click(QcGamepadButton::LeftShoulder);
        click(QcGamepadButton::LeftShoulder);
        QCOMPARE(tabs->property("currentIndex").toInt(), 2);

        screen()->setProperty("takeTabs", true);
        click(QcGamepadButton::RightShoulder);
        QCOMPARE(tabs->property("currentIndex").toInt(), 2);
        QCOMPARE(screen()->property("lastClick").toString(), u"handler"_s);
    }

    // A on a text field asks for the keyboard instead of clicking; A does not jump a slider.
    void textFieldsAskForTheKeyboard()
    {
        QSignalSpy keyboard(m_ui.get(), &QcGamepadUi::keyboardRequested);
        click(QcGamepadButton::DpadDown);
        move(QcGamepadButton::DpadDown, 2);
        QCOMPARE(focus(), u"name"_s);
        click(QcGamepadButton::South);
        QCOMPARE(keyboard.size(), 1);
        QCOMPARE(keyboard.first().first().value<QQuickItem *>(), item(u"name"_s));

        move(QcGamepadButton::DpadDown);
        QCOMPARE(focus(), u"slider"_s);
        click(QcGamepadButton::South);
        QCOMPARE(item(u"slider"_s)->property("value").toDouble(), 5.0);
    }

    // Left and right change a horizontal slider (as its keys: moved() reports it) and a spin box
    // (valueModified()); up and down still move on.
    void slidersAndSpinBoxesTakeLeftAndRight()
    {
        QQuickItem *slider = item(u"slider"_s);
        click(QcGamepadButton::DpadDown);
        move(QcGamepadButton::DpadDown, 3);
        QCOMPARE(focus(), u"slider"_s);
        move(QcGamepadButton::DpadRight);
        QVERIFY2(slider->property("value").toDouble() > 5.0, qPrintable(slider->property("value").toString()));
        QCOMPARE(screen()->property("lastClick").toString(), u"slider"_s);
        move(QcGamepadButton::DpadLeft, 2);
        QVERIFY(slider->property("value").toDouble() < 5.0);
        QCOMPARE(focus(), u"slider"_s);
        move(QcGamepadButton::DpadDown);
        QCOMPARE(focus(), u"openPopup"_s);

        QQuickItem *spin = item(u"spin"_s);
        move(QcGamepadButton::DpadRight);
        QVERIFY2(QcFocusCandidates::contains(spin, m_window->activeFocusItem()), qPrintable(focus()));
        move(QcGamepadButton::DpadRight);
        QCOMPARE(spin->property("value").toInt(), 9);
        QCOMPARE(screen()->property("lastClick").toString(), u"spin"_s);
        move(QcGamepadButton::DpadLeft, 3);
        QCOMPARE(spin->property("value").toInt(), 6);
        QVERIFY(QcFocusCandidates::contains(spin, m_window->activeFocusItem()));
    }

    // A direction and the right stick held while the world had the pad do nothing when a screen
    // opens (no repeat, no scrolling) until they were let go.
    void heldInputFromTheWorldWaitsForNeutral()
    {
        click(QcGamepadButton::DpadDown);
        QCOMPARE(focus(), u"middle"_s);
        QQuickItem *list = item(u"list"_s);

        m_screensOpen = false;
        m_source.setPressed(m_pad, QcGamepadButton::DpadUp, true);
        m_source.setAxis(m_pad, QcGamepadAxis::RightY, 1.0f);
        m_screensOpen = true;
        const qint64 start = m_ui->menuInput().now();
        for (int step = 1; step <= 10; ++step) {
            m_ui->menuInput().advance(start + static_cast<qint64>(step) * 100);
        }
        QCOMPARE(focus(), u"middle"_s);
        QCOMPARE(list->property("contentY").toDouble(), 0.0);

        // Let go, then the same input acts.
        m_source.setPressed(m_pad, QcGamepadButton::DpadUp, false);
        m_source.setAxis(m_pad, QcGamepadAxis::RightY, 0.0f);
        move(QcGamepadButton::DpadUp);
        QCOMPARE(focus(), u"first"_s);
    }

    // Scrolling a list with the right stick takes the focus along to a row shown whole, and the
    // ring stays inside the list.
    void scrollingKeepsTheFocusInView()
    {
        click(QcGamepadButton::DpadDown);
        move(QcGamepadButton::DpadUp);
        move(QcGamepadButton::DpadRight, 4);
        move(QcGamepadButton::DpadDown);
        QCOMPARE(focus(), u"row0"_s);

        QQuickItem *list = item(u"list"_s);
        m_source.setAxis(m_pad, QcGamepadAxis::RightY, 1.0f);
        const qint64 start = m_ui->menuInput().now();
        for (int step = 1; step <= 4; ++step) {
            m_ui->menuInput().advance(start + static_cast<qint64>(step) * 100);
        }
        m_source.setAxis(m_pad, QcGamepadAxis::RightY, 0.0f);
        QVERIFY(list->property("contentY").toDouble() > 300.0);
        QVERIFY2(focus() != u"row0"_s, qPrintable(focus()));
        QVERIFY(QcFocusCandidates::wholeInView(m_window->activeFocusItem()));
        const QRectF shown = QcFocusCandidates::sceneRect(list);
        QVERIFY(m_ui->focusShown());
        QVERIFY(shown.contains(m_ui->focusRect()));
    }

    // The right stick's x scrolls sideways by the rules of its y: the page around the focus, else the
    // scope's first one, with the focus kept in view; a list that snaps and a page without room to
    // the sides stay as they are.
    void sidewaysScrollingFollowsTheVerticalRules()
    {
        screen()->setProperty("sideways", true);
        click(QcGamepadButton::DpadDown);
        const auto pushRight = [this] {
            m_source.setAxis(m_pad, QcGamepadAxis::RightX, 1.0f);
            const qint64 start = m_ui->menuInput().now();
            for (int step = 1; step <= 4; ++step) {
                m_ui->menuInput().advance(start + static_cast<qint64>(step) * 100);
            }
            m_source.setAxis(m_pad, QcGamepadAxis::RightX, 0.0f);
            QCoreApplication::processEvents();
        };

        QQuickItem *wide = item(u"wide"_s);
        QVERIFY(wide && itemBelow(wide, u"wide0"_s));
        itemBelow(wide, u"wide0"_s)->forceActiveFocus(Qt::TabFocusReason);
        QCOMPARE(focus(), u"wide0"_s);
        pushRight();
        QVERIFY(wide->property("contentX").toDouble() > 300.0);
        QVERIFY2(focus() != u"wide0"_s && focus().startsWith(u"wide"_s), qPrintable(focus()));
        QVERIFY(QcFocusCandidates::wholeInView(m_window->activeFocusItem()));

        // The focus outside any page: the scope's first one moves.
        wide->setProperty("contentX", 0.0);
        item(u"first"_s)->forceActiveFocus(Qt::TabFocusReason);
        pushRight();
        QVERIFY(wide->property("contentX").toDouble() > 300.0);
        QCOMPARE(focus(), u"first"_s);

        // A strip that snaps keeps its place; a list without room to the sides as well.
        QQuickItem *strip = item(u"strip"_s);
        QTRY_VERIFY(itemBelow(strip, u"strip0"_s));
        itemBelow(strip, u"strip0"_s)->forceActiveFocus(Qt::TabFocusReason);
        pushRight();
        QCOMPARE(strip->property("contentX").toDouble(), 0.0);
        QQuickItem *list = item(u"list"_s);
        QVERIFY(itemBelow(list, u"row0"_s));
        itemBelow(list, u"row0"_s)->forceActiveFocus(Qt::TabFocusReason);
        pushRight();
        QCOMPARE(list->property("contentX").toDouble(), 0.0);
    }

    // The game's own return of the captured mouse to the window's centre and a mouse that hardly
    // moved leave the pad in use; a real move gives the input back.
    void mouseWarpKeepsThePad()
    {
        QTest::mouseMove(m_window, QPoint(100, 500));
        click(QcGamepadButton::DpadDown);
        QVERIFY(m_ui->active());
        QTest::mouseMove(m_window, QPoint(m_window->width() / 2, m_window->height() / 2));
        QVERIFY(m_ui->active());
        QTest::mouseMove(m_window, QPoint(102, 501));
        QVERIFY(m_ui->active());
        QTest::mouseMove(m_window, QPoint(160, 520));
        QVERIFY(!m_ui->active());
    }

    // The ring's work per frame on a screen with a thousand slots: it maps the focused item
    // without walking the screen (the scope is kept); the walks it saves are measured beside it.
    void largeScreenCostsLittlePerFrame()
    {
        screen()->setProperty("extraSlots", 1000);
        click(QcGamepadButton::DpadDown);
        QCOMPARE(focus(), u"middle"_s);
        QCOMPARE(QcFocusCandidates::candidates(QcFocusCandidates::scope(m_window)).size() > 1000, true);

        constexpr int frames = 200;
        QElapsedTimer timer;
        timer.start();
        for (int frame = 0; frame < frames; ++frame) {
            QcFocusCandidates::scope(m_window);
        }
        const qint64 scopeNs = timer.nsecsElapsed() / frames;
        timer.restart();
        for (int frame = 0; frame < frames; ++frame) {
            QcFocusCandidates::candidates(item(u"screen"_s));
        }
        const qint64 walkNs = timer.nsecsElapsed() / frames;
        timer.restart();
        for (int frame = 0; frame < frames; ++frame) {
            emit m_window->afterAnimating();
        }
        const qint64 frameNs = timer.nsecsElapsed() / frames;
        qInfo("per frame with 1000 slots: ring %lld ns; scope lookup %lld ns, full walk %lld ns", frameNs, scopeNs, walkNs);
        QCOMPARE(focus(), u"middle"_s);
        QVERIFY(m_ui->focusShown());
    }

    // Down a list the focus scrolls it; a held direction repeats.
    void listScrollsWithTheFocus()
    {
        click(QcGamepadButton::DpadDown);
        move(QcGamepadButton::DpadUp);
        move(QcGamepadButton::DpadRight, 4);
        QCOMPARE(focus(), u"TabButton:One"_s);
        move(QcGamepadButton::DpadDown);
        QCOMPARE(focus(), u"row0"_s);
        QQuickItem *list = item(u"list"_s);
        move(QcGamepadButton::DpadDown, 8);
        QCOMPARE(focus(), u"row8"_s);
        QVERIFY(list->property("contentY").toDouble() > 100.0);
        // Rows have no object parent: the focused one is found through the window.
        const QRectF row = m_window->activeFocusItem()->mapRectToItem(list, QRectF(0, 0, 300, 40));
        QVERIFY(row.top() >= 0.0 && row.bottom() <= list->height());

        m_source.setPressed(m_pad, QcGamepadButton::DpadDown, true);
        const qint64 start = m_ui->menuInput().now();
        m_ui->menuInput().advance(start + QcGamepadMenuInput::repeatDelayMs + 2 * QcGamepadMenuInput::repeatIntervalMs + 20);
        m_source.setPressed(m_pad, QcGamepadButton::DpadDown, false);
        QCOMPARE(focus(), u"row12"_s);
    }

    // An open popup holds the focus; B closes it and the focus returns to the screen.
    void popupKeepsTheFocus()
    {
        click(QcGamepadButton::DpadDown);
        move(QcGamepadButton::DpadDown, 5);
        QCOMPARE(focus(), u"openPopup"_s);
        click(QcGamepadButton::South);
        QObject *popup = m_window->findChild<QObject *>(u"popup"_s);
        QVERIFY(popup);
        QTRY_VERIFY(popup->property("opened").toBool());

        // The focus moves into the popup by itself while the pad leads.
        QTRY_COMPARE(focus(), u"popupYes"_s);
        move(QcGamepadButton::DpadDown);
        QCOMPARE(focus(), u"popupNo"_s);
        move(QcGamepadButton::DpadRight, 3);
        QCOMPARE(focus(), u"popupNo"_s);
        click(QcGamepadButton::East);
        QTRY_VERIFY(!popup->property("visible").toBool());
        QTRY_COMPARE(focus(), u"openPopup"_s);
    }

    // Without the pad in use nothing of it runs per frame or per event: the frame hook and the
    // application's event filter are attached only while the pad leads.
    void restingPadCostsNothingPerFrame()
    {
        constexpr int frames = 100;
        for (int frame = 0; frame < frames; ++frame) {
            emit m_window->afterAnimating();
        }
        QTest::mouseMove(m_window, QPoint(300, 300));
        QTest::keyClick(m_window, Qt::Key_A);
        QCoreApplication::processEvents();
        QCOMPARE(m_ui->hookCalls(), 0u);

        click(QcGamepadButton::DpadDown);
        QVERIFY(m_ui->active());
        const quint64 leading = m_ui->hookCalls();
        emit m_window->afterAnimating();
        QVERIFY(m_ui->hookCalls() > leading);

        QTest::keyClick(m_window, Qt::Key_A);
        QVERIFY(!m_ui->active());
        const quint64 resting = m_ui->hookCalls();
        for (int frame = 0; frame < frames; ++frame) {
            emit m_window->afterAnimating();
        }
        QTest::mouseMove(m_window, QPoint(500, 400));
        QCoreApplication::processEvents();
        QCOMPARE(m_ui->hookCalls(), resting);
    }

    // The keyboard or the mouse used again hides the ring; the pad shows it again.
    void ringFollowsTheDeviceInUse()
    {
        click(QcGamepadButton::DpadDown);
        QVERIFY(m_ui->focusShown());
        QTest::keyClick(m_window, Qt::Key_A);
        QVERIFY(!m_ui->active());
        QVERIFY(!m_ui->focusShown());
        m_source.setAxis(m_pad, QcGamepadAxis::RightX, 0.9f);
        QVERIFY(m_ui->active());
        m_source.setAxis(m_pad, QcGamepadAxis::RightX, 0.0f);
        QTest::mouseMove(m_window, QPoint(700, 500));
        QVERIFY(!m_ui->active());
    }
};

QTEST_MAIN(TestGamepadScreens)
#include "test_gamepad_screens.moc"
