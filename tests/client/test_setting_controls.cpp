// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/input/QcGamepadUi.h"
#include "clientcore/input/QcGamepadControls.h"
#include "clientcore/input/QcVirtualGamepadSource.h"

#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTest>

#include <functional>

using namespace Qt::StringLiterals;

namespace {

// A screen built like the settings: the category list on the left, whose pad commands the screen's
// content passes on as SettingsScreen does, and a switch, segments and a check box.
const char *const sceneQml = R"(
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

ApplicationWindow {
    width: 800
    height: 500
    visible: true

    RowLayout {
        objectName: "content"
        anchors.fill: parent
        focus: true

        function gamepadCommand(command: string): bool {
            return nav.takeCommand(command);
        }

        NavList {
            id: nav

            objectName: "nav"
            Layout.preferredWidth: 200
            Layout.alignment: Qt.AlignTop
            model: ["One", "Two", "Three"]
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop

            SettingSwitch {
                property int toggles: 0

                objectName: "switchRow"
                label: "Switch"
                onToggled: toggles++
            }
            SettingChoice {
                objectName: "choice"
                label: "Choice"
                model: [{ text: "A", value: "a" }, { text: "B", value: "b" }, { text: "C", value: "c" }]
                currentValue: "a"
                onActivated: value => currentValue = value
            }
            SettingCheck {
                objectName: "check"
                text: "Check"
            }
            // Room for both segments, and too little room (they shrink to their own texts).
            Segments {
                objectName: "wideSegments"
                model: [{ text: "Info", value: 0 }, { text: "Warnings and errors", value: 1 }]
            }
            Item {
                Layout.preferredWidth: 40
                Layout.preferredHeight: 30

                Segments {
                    objectName: "narrowSegments"
                    anchors.fill: parent
                    model: [{ text: "Info", value: 0 }, { text: "Warnings and errors", value: 1 }]
                }
            }
            SettingGroup {
                title: "Group"
                help: "Help for the whole group"
            }
        }
    }
}
)";

} // namespace

// The keys and pad commands of the settings' controls (decision E-845, docs/UI-Leitfaden.md 6):
// Ctrl+Tab and Ctrl+Shift+Tab switch the category, on a Mac with the Control key Qt reports as
// Meta, also while a control inside has the focus, and the pad's shoulders do the same through the
// screen's gamepadCommand(); Enter switches switches and check boxes as Space does; the arrow keys
// choose the segment beside the chosen one and stop at the ends. Segments are equally wide with
// room and shrink towards their own texts without; a group shows its help below its heading.
class TestSettingControls : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QQuickWindow *m_window = nullptr;

    // The first item below `root` (or `root` itself) that `matches`; through the item tree, since
    // repeaters' delegates have no object parent.
    static QQuickItem *find(QQuickItem *root, const std::function<bool(QQuickItem *)> &matches)
    {
        if (matches(root)) {
            return root;
        }
        const QList<QQuickItem *> children = root->childItems();
        for (QQuickItem *child : children) {
            if (QQuickItem *found = find(child, matches)) {
                return found;
            }
        }
        return nullptr;
    }

    QQuickItem *named(const QString &name) const
    {
        return find(m_window->contentItem(), [&name](QQuickItem *item) { return item->objectName() == name; });
    }

    // The button below `root` with the text `text` (a segment, a category).
    static QQuickItem *button(QQuickItem *root, const QString &text)
    {
        return find(root, [&text](QQuickItem *item) { return item->inherits("QQuickAbstractButton") && item->property("text").toString() == text; });
    }

    int category() const { return named(u"nav"_s)->property("currentIndex").toInt(); }

private slots:
    void initTestCase() { QQuickStyle::setStyle(u"Basic"_s); }

    void init()
    {
        m_engine = std::make_unique<QQmlApplicationEngine>();
        m_engine->loadData(sceneQml);
        QVERIFY(!m_engine->rootObjects().isEmpty());
        m_window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().first());
        QVERIFY(m_window);
        QVERIFY(QTest::qWaitForWindowExposed(m_window));
        m_window->requestActivate();
        named(u"content"_s)->forceActiveFocus();
    }

    void cleanup()
    {
        m_engine.reset();
        m_window = nullptr;
    }

    void ctrlTabSwitchesTheCategory()
    {
        QCOMPARE(category(), 0);
        QTest::keyClick(m_window, Qt::Key_Tab, Qt::ControlModifier);
        QCOMPARE(category(), 1);
        // The Control key of a Mac.
        QTest::keyClick(m_window, Qt::Key_Tab, Qt::MetaModifier);
        QCOMPARE(category(), 2);
        QTest::keyClick(m_window, Qt::Key_Tab, Qt::ControlModifier);
        QCOMPARE(category(), 0);
        QTest::keyClick(m_window, Qt::Key_Backtab, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(category(), 2);
        QTest::keyClick(m_window, Qt::Key_Backtab, Qt::MetaModifier | Qt::ShiftModifier);
        QCOMPARE(category(), 1);

        // Tab alone moves the focus, not the category; a control with the focus passes Ctrl+Tab on.
        QTest::keyClick(m_window, Qt::Key_Tab);
        QCOMPARE(category(), 1);
        QQuickItem *toggle = find(named(u"switchRow"_s), [](QQuickItem *item) { return item->inherits("QQuickSwitch"); });
        QVERIFY(toggle);
        toggle->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(m_window, Qt::Key_Tab, Qt::MetaModifier);
        QCOMPARE(category(), 2);
        QVERIFY(toggle->hasActiveFocus());
    }

    void padShouldersSwitchTheCategory()
    {
        QcVirtualGamepadSource source;
        QcGamepadControls world;
        const int pad = source.connectPad(u"pad"_s, QcGamepadType::Xbox);
        QcGamepadUi ui(world, [] { return true; });
        ui.setSource(&source);
        ui.setWindow(m_window);
        ui.setIgnoreWindowFocus(true);
        const auto click = [&source, pad](QcGamepadButton button) {
            source.setPressed(pad, button, true);
            source.setPressed(pad, button, false);
            QCoreApplication::processEvents();
        };

        click(QcGamepadButton::RightShoulder);
        QCOMPARE(category(), 1);
        click(QcGamepadButton::RightShoulder);
        click(QcGamepadButton::RightShoulder);
        QCOMPARE(category(), 0);
        click(QcGamepadButton::LeftShoulder);
        QCOMPARE(category(), 2);
        source.disconnectPad(pad);
    }

    void enterSwitchesSwitchesAndCheckBoxes()
    {
        QQuickItem *row = named(u"switchRow"_s);
        QQuickItem *toggle = find(row, [](QQuickItem *item) { return item->inherits("QQuickSwitch"); });
        QVERIFY(toggle);
        toggle->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(m_window, Qt::Key_Return);
        QVERIFY(toggle->property("checked").toBool());
        QCOMPARE(row->property("toggles").toInt(), 1);
        QTest::keyClick(m_window, Qt::Key_Enter);
        QVERIFY(!toggle->property("checked").toBool());
        QCOMPARE(row->property("toggles").toInt(), 2);

        QQuickItem *check = named(u"check"_s);
        check->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(m_window, Qt::Key_Return);
        QVERIFY(check->property("checked").toBool());
        QTest::keyClick(m_window, Qt::Key_Space);
        QVERIFY(!check->property("checked").toBool());
    }

    void arrowsChooseTheNextSegment()
    {
        QQuickItem *choice = named(u"choice"_s);
        button(choice, u"A"_s)->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(m_window, Qt::Key_Right);
        QCOMPARE(choice->property("currentValue").toString(), u"b"_s);
        QVERIFY(button(choice, u"B"_s)->hasActiveFocus());
        QTest::keyClick(m_window, Qt::Key_Right);
        QTest::keyClick(m_window, Qt::Key_Right);
        QCOMPARE(choice->property("currentValue").toString(), u"c"_s);
        QVERIFY(button(choice, u"C"_s)->hasActiveFocus());
        QTest::keyClick(m_window, Qt::Key_Left);
        QCOMPARE(choice->property("currentValue").toString(), u"b"_s);
        QCOMPARE(category(), 0);
    }

    void segmentsAreEquallyWideWithRoom()
    {
        const auto widthOf = [this](const QString &segments, const QString &text) {
            return button(named(segments), text)->width();
        };
        QTRY_VERIFY(widthOf(u"wideSegments"_s, u"Info"_s) > 0);
        // Equal up to the rounding of the row's width to whole pixels.
        QVERIFY(qAbs(widthOf(u"wideSegments"_s, u"Info"_s) - widthOf(u"wideSegments"_s, u"Warnings and errors"_s)) <= 1);
        QVERIFY(widthOf(u"narrowSegments"_s, u"Info"_s) < widthOf(u"narrowSegments"_s, u"Warnings and errors"_s));
    }

    void groupShowsItsHelp()
    {
        QQuickItem *help = find(m_window->contentItem(),
                                [](QQuickItem *item) { return item->property("text").toString() == u"Help for the whole group"_s; });
        QVERIFY(help);
        QVERIFY(help->isVisible());
        QVERIFY(help->height() > 0);
    }
};

QTEST_MAIN(TestSettingControls)
#include "test_setting_controls.moc"
