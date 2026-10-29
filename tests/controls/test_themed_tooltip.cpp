// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;

namespace {

// Two slots of an inventory's size with the tooltip as InventorySlot declares it: one item with a
// one-line name, one with a custom name in italics and two lines below it, wrapped at the
// tooltip's width.
const char *const sceneQml = R"(
import QtQuick
import QtQuick.Controls
import QtCraft

ApplicationWindow {
    width: 1024
    height: 640
    visible: true

    Rectangle {
        x: 100
        y: 100
        width: 40
        height: 40
        color: "grey"

        ThemedToolTip {
            objectName: "single"
            shown: true
            text: "Diamond sword"
        }
    }

    Rectangle {
        x: 300
        y: 100
        width: 40
        height: 40
        color: "grey"

        ThemedToolTip {
            objectName: "multi"
            shown: true
            italicTitle: true
            text: "Old faithful\nDurability: 100 / 250\nA very long line of text that runs on and on past the width a tooltip may take, so it has to wrap onto further lines"
        }
    }
}
)";

QStringList warnings;

void collectWarnings(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    if (type == QtWarningMsg) {
        warnings.append(message);
    }
}

} // namespace

// ThemedToolTip (decisions E-279, E-874): a shown tooltip has a size and shows its text; its lines
// wrap at a width of their own, not at the tooltip's (which follows from the lines), so no bound
// goes round in a circle and settles at 0.
class TestThemedTooltip : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QQuickWindow *m_window = nullptr;

    // The popup a ThemedToolTip shows and the texts of its lines.
    QObject *tip(const QString &name) const
    {
        const QObject *holder = m_engine->rootObjects().first()->findChild<QObject *>(name);
        return holder ? holder->property("item").value<QObject *>() : nullptr;
    }

    static QList<QQuickItem *> lines(const QObject *popup)
    {
        const auto *column = popup->property("contentItem").value<QQuickItem *>();
        return column ? column->childItems() : QList<QQuickItem *>{};
    }

private slots:
    void initTestCase()
    {
        QQuickStyle::setStyle(u"Basic"_s);
        qInstallMessageHandler(collectWarnings);
        m_engine = std::make_unique<QQmlApplicationEngine>();
        m_engine->loadData(sceneQml);
        QVERIFY(!m_engine->rootObjects().isEmpty());
        m_window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().first());
        QVERIFY(m_window);
        QVERIFY(QTest::qWaitForWindowExposed(m_window));
    }

    void cleanupTestCase()
    {
        m_engine.reset();
        qInstallMessageHandler(nullptr);
    }

    void oneLineShowsTheName()
    {
        QObject *popup = tip(u"single"_s);
        QVERIFY(popup);
        QTRY_VERIFY(popup->property("opened").toBool());
        QVERIFY(popup->property("width").toReal() > 20.0);
        QVERIFY(popup->property("height").toReal() > 10.0);

        const QList<QQuickItem *> shown = lines(popup);
        QCOMPARE(shown.size(), 2);
        QCOMPARE(shown[0]->property("text").toString(), u"Diamond sword"_s);
        QVERIFY(shown[0]->width() > 20.0);
        QCOMPARE(shown[0]->width(), shown[0]->implicitWidth());
        QVERIFY(!shown[1]->isVisible());
    }

    void severalLinesWrapAtTheTooltipWidth()
    {
        QObject *popup = tip(u"multi"_s);
        QVERIFY(popup);
        QTRY_VERIFY(popup->property("opened").toBool());

        const QList<QQuickItem *> shown = lines(popup);
        QCOMPARE(shown.size(), 2);
        QCOMPARE(shown[0]->property("text").toString(), u"Old faithful"_s);
        QVERIFY(shown[0]->property("font").value<QFont>().italic());
        QVERIFY(shown[0]->width() > 20.0);
        QVERIFY(shown[1]->isVisible());
        QVERIFY(shown[1]->property("text").toString().startsWith(u"Durability: 100 / 250\n"_s));
        // The long line wraps: the text is narrower than its one-line width and several lines high.
        QVERIFY(shown[1]->width() > 100.0);
        QVERIFY(shown[1]->width() < shown[1]->implicitWidth());
        QVERIFY(shown[1]->property("lineCount").toInt() >= 3);
        QVERIFY(popup->property("width").toReal() >= shown[1]->width());
        QVERIFY(popup->property("width").toReal() < m_window->width());
    }

    void noBindingLoop()
    {
        for (const QString &warning : std::as_const(warnings)) {
            QVERIFY2(!warning.contains(u"Binding loop"_s, Qt::CaseInsensitive), qPrintable(warning));
        }
    }
};

QTEST_MAIN(TestThemedTooltip)
#include "test_themed_tooltip.moc"
