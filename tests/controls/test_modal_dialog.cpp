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
#include <QWheelEvent>

#include <functional>

using namespace Qt::StringLiterals;

namespace {

// A screen built like the main menu: a side bar on the left and a page on the right that declares
// the dialog, as the character page declares its leave prompt.
const char *const sceneQml = R"(
import QtQuick
import QtQuick.Controls
import QtCraft

ApplicationWindow {
    width: 1000
    height: 600
    visible: true

    property var keysBelow: []

    // A page below the dialog: it had the focus when the dialog opened and notes every key it gets.
    FocusScope {
        objectName: "page"
        anchors.fill: parent
        focus: true
        Keys.onPressed: event => keysBelow = keysBelow.concat([event.key])
    }

    // A long list beside the dialog, as the handbook's entries or the settings scroll.
    Flickable {
        objectName: "list"
        x: 860
        width: 140
        height: 600
        contentHeight: 3000
    }

    NavList {
        id: nav

        objectName: "nav"
        width: 200
        model: ["One", "Two", "Three"]
    }

    Item {
        x: 400
        width: 600
        height: 300

        UnsavedChangesDialog {
            objectName: "dialog"
        }
    }
}
)";

} // namespace

// Dialogs that need an answer are modal for the whole window (decision E-852,
// docs/UI-Leitfaden.md 6.0): ModalDialog centres them over the window rather than over the page
// that declares them, and while one is open a click or the pad's directions on the side bar do
// nothing; the focus stays on the dialog's buttons, and neither the wheel nor a key reaches the page.
class TestModalDialog : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QQuickWindow *m_window = nullptr;
    QObject *m_dialog = nullptr;

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

    QQuickItem *button(const QString &text) const
    {
        return find(m_window->contentItem(),
                    [&text](QQuickItem *item) { return item->inherits("QQuickAbstractButton") && item->property("text").toString() == text; });
    }

    int category() const { return m_engine->rootObjects().first()->findChild<QObject *>(u"nav"_s)->property("currentIndex").toInt(); }

    bool focusOnDialog() const
    {
        const QQuickItem *focus = m_window->activeFocusItem();
        static const QStringList buttons{u"discardChangesButton"_s, u"keepEditingButton"_s, u"saveChangesButton"_s};
        return focus && buttons.contains(focus->objectName());
    }

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
        m_dialog = m_engine->rootObjects().first()->findChild<QObject *>(u"dialog"_s);
        QVERIFY(m_dialog);
        QMetaObject::invokeMethod(m_dialog, "open");
        QTRY_VERIFY(m_dialog->property("opened").toBool());
    }

    void cleanup()
    {
        m_engine.reset();
        m_window = nullptr;
        m_dialog = nullptr;
    }

    void centredOverTheWindow()
    {
        const qreal x = m_dialog->property("x").toReal();
        const qreal y = m_dialog->property("y").toReal();
        const qreal width = m_dialog->property("width").toReal();
        const qreal height = m_dialog->property("height").toReal();
        QVERIFY(qAbs(x + width / 2 - m_window->width() / 2.0) <= 1.0);
        QVERIFY(qAbs(y + height / 2 - m_window->height() / 2.0) <= 1.0);
        QVERIFY(m_dialog->property("dim").toBool());
    }

    void sideBarTakesNoClick()
    {
        QQuickItem *two = button(u"Two"_s);
        QVERIFY(two);
        const QPoint centre = two->mapToScene(QPointF(two->width() / 2, two->height() / 2)).toPoint();
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, centre);
        QCOMPARE(category(), 0);
        QVERIFY(m_dialog->property("visible").toBool());
    }

    void padStaysOnTheDialog()
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

        for (const QcGamepadButton direction : {QcGamepadButton::DpadLeft, QcGamepadButton::DpadLeft, QcGamepadButton::DpadLeft,
                                                QcGamepadButton::DpadUp, QcGamepadButton::DpadDown}) {
            click(direction);
            QVERIFY2(focusOnDialog(), qPrintable(m_window->activeFocusItem() ? m_window->activeFocusItem()->objectName() : u"none"_s));
        }
        click(QcGamepadButton::RightShoulder);
        QCOMPARE(category(), 0);
        source.disconnectPad(pad);
    }

    // The wheel over the page beside the dialog scrolls nothing below it.
    void wheelReachesNothingBelow()
    {
        QObject *list = m_engine->rootObjects().first()->findChild<QObject *>(u"list"_s);
        QVERIFY(list);
        const QPoint point(930, 300);
        QWheelEvent wheel(point, m_window->mapToGlobal(point), QPoint(), QPoint(0, -360), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
                          false);
        QCoreApplication::sendEvent(m_window, &wheel);
        QCoreApplication::processEvents();
        QCOMPARE(list->property("contentY").toReal(), 0.0);
    }

    // Plain keys (the handbook's H) and Esc stay with the dialog; the page below gets none.
    void keysReachNothingBelow()
    {
        QObject *root = m_engine->rootObjects().first();
        QTest::keyClick(m_window, Qt::Key_H);
        QTest::keyClick(m_window, Qt::Key_Escape);
        QCOMPARE(root->property("keysBelow").toList().size(), 0);
    }

    void tabStaysOnTheDialog()
    {
        for (int press = 0; press < 5; ++press) {
            QTest::keyClick(m_window, Qt::Key_Tab);
            QVERIFY(focusOnDialog());
        }
    }
};

QTEST_MAIN(TestModalDialog)
#include "test_modal_dialog.moc"
