// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/admin/QcAdminCommandList.h"

#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTest>

#include <functional>

using namespace Qt::StringLiterals;

namespace {

// The command list of "Manage server" in a small window, far more rows than fit.
const char *const sceneQml = R"(
import QtQuick
import QtQuick.Controls
import QtCraft

ApplicationWindow {
    id: root

    required property AdminCommandList commands

    width: 600
    height: 400
    visible: true

    AdminCommandIndex {
        anchors.fill: parent
        commands: root.commands
    }
}
)";

constexpr int commandCount = 60;

} // namespace

// The keyboard walks the command list of "Manage server" (decision E-911, docs/UI-Leitfaden.md 6):
// Tab and Down go from row to row through all of them, Shift+Tab and Up back, also past the rows the
// list made at first, and the focused row is always scrolled into view; descriptions take the row's
// width.
class TestAdminCommandIndex : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<QcAdminCommandList> m_commands;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QQuickWindow *m_window = nullptr;

    // The first item below `root` (or `root` itself) that `matches`; through the item tree, since
    // the list's delegates have no object parent.
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

    static QString commandName(int index) { return u"c%1"_s.arg(index, 2, 10, QChar(u'0')); }

private slots:
    void initTestCase() { QQuickStyle::setStyle(u"Basic"_s); }

    void init()
    {
        m_commands = std::make_unique<QcAdminCommandList>();
        QcMsg::AdminCatalogue catalogue;
        for (int index = 0; index < commandCount; ++index) {
            QcMsg::AdminCatalogue::Command command;
            command.name = commandName(index);
            command.usage = u"Usage: /"_s + commandName(index);
            command.description = u"Does the thing number %1, which takes a few words to say."_s.arg(index);
            catalogue.commands.append(command);
        }
        m_commands->setCatalogue(catalogue);
        QCOMPARE(m_commands->count(), commandCount);

        m_engine = std::make_unique<QQmlApplicationEngine>();
        m_engine->setInitialProperties({{u"commands"_s, QVariant::fromValue(m_commands.get())}});
        m_engine->loadData(sceneQml);
        QVERIFY(!m_engine->rootObjects().isEmpty());
        m_window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().first());
        QVERIFY(m_window);
        QVERIFY(QTest::qWaitForWindowExposed(m_window));
        m_window->requestActivate();
    }

    void cleanup()
    {
        m_engine.reset();
        m_commands.reset();
        m_window = nullptr;
    }

    void tabWalksEveryRowInView()
    {
        QQuickItem *list = named(u"adminCommandIndex"_s);
        QVERIFY(list);
        QQuickItem *first = named(u"adminCommand_"_s + commandName(0));
        QVERIFY(first);
        first->forceActiveFocus(Qt::TabFocusReason);

        for (int index = 0; index < commandCount; ++index) {
            const QString expected = u"adminCommand_"_s + commandName(index);
            QTRY_COMPARE(m_window->activeFocusItem() ? m_window->activeFocusItem()->objectName() : QString(), expected);
            const QQuickItem *row = m_window->activeFocusItem();
            const QRectF view = list->mapRectToScene(QRectF(0.0, 0.0, list->width(), list->height()));
            const QRectF shown = row->mapRectToScene(QRectF(0.0, 0.0, row->width(), row->height()));
            QVERIFY2(view.contains(shown), qPrintable(u"%1 at y %2..%3, the list %4..%5"_s.arg(expected)
                                                          .arg(shown.top())
                                                          .arg(shown.bottom())
                                                          .arg(view.top())
                                                          .arg(view.bottom())));
            QTest::keyClick(m_window, Qt::Key_Tab);
        }
        // Shift+Tab and the arrow keys walk back.
        named(u"adminCommand_"_s + commandName(commandCount - 1))->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(m_window, Qt::Key_Backtab, Qt::ShiftModifier);
        QTRY_COMPARE(m_window->activeFocusItem()->objectName(), u"adminCommand_"_s + commandName(commandCount - 2));
        for (int step = 0; step < 50; ++step) {
            QTest::keyClick(m_window, Qt::Key_Up);
        }
        QTRY_COMPARE(m_window->activeFocusItem()->objectName(), u"adminCommand_"_s + commandName(commandCount - 52));
        QVERIFY(list->mapRectToScene(QRectF(0.0, 0.0, list->width(), list->height()))
                    .contains(m_window->activeFocusItem()->mapRectToScene(QRectF(0.0, 0.0, m_window->activeFocusItem()->width(), m_window->activeFocusItem()->height()))));
    }

    // A description takes the row's width: it wraps only where the row ends.
    void descriptionsUseTheRowsWidth()
    {
        QQuickItem *row = named(u"adminCommand_"_s + commandName(0));
        QVERIFY(row);
        const QString description = m_commands->shown().constFirst().description;
        QQuickItem *subtitle = find(row, [&description](QQuickItem *item) { return item->property("text").toString() == description; });
        QVERIFY(subtitle);
        QVERIFY2(subtitle->width() > row->width() * 0.8, qPrintable(u"%1 of %2"_s.arg(subtitle->width()).arg(row->width())));
    }
};

QTEST_MAIN(TestAdminCommandIndex)
#include "test_admin_command_index.moc"
