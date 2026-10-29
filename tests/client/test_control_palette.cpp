// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/models/QcControlPalette.h"
#include "clientcore/settings/QcUiPalette.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

using namespace Qt::StringLiterals;

// The game's control palette reaches Qt Quick windows (decision E-279).
class TestControlPalette : public QObject
{
    Q_OBJECT

private slots:
    // Every role of the window's palette takes the game's colour, in every colour group.
    void windowTakesTheGamePalette()
    {
        QQuickWindow window;
        QVERIFY(QcControlPalette::applyTo(window));

        auto *palette = window.property("palette").value<QObject *>();
        QVERIFY(palette);
        const QPalette expected = QcUiPalette::controlPalette();
        QCOMPARE(palette->property("toolTipBase").value<QColor>(), expected.color(QPalette::ToolTipBase));
        QCOMPARE(palette->property("toolTipText").value<QColor>(), expected.color(QPalette::ToolTipText));
        QCOMPARE(palette->property("alternateBase").value<QColor>(), expected.color(QPalette::AlternateBase));
        auto *disabled = palette->property("disabled").value<QObject *>();
        QVERIFY(disabled);
        QCOMPARE(disabled->property("text").value<QColor>(), expected.color(QPalette::Disabled, QPalette::Text));
        auto *inactive = palette->property("inactive").value<QObject *>();
        QVERIFY(inactive);
        QCOMPARE(inactive->property("highlight").value<QColor>(), expected.color(QPalette::Inactive, QPalette::Highlight));
    }

    // A popup in the window (the attached tooltip is one) takes the window's palette, which wins
    // over the system palette for tooltips.
    void tooltipInheritsTheWindowPalette()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import QtQuick.Controls
            Item {
                property alias tip: tip
                ToolTip { id: tip; text: "x" }
            })"_ba,
                          QUrl());
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));

        QQuickWindow window;
        QVERIFY(QcControlPalette::applyTo(window));
        qobject_cast<QQuickItem *>(root.get())->setParentItem(window.contentItem());
        auto *tip = root->property("tip").value<QObject *>();
        QVERIFY(tip);
        auto *palette = tip->property("palette").value<QObject *>();
        QVERIFY(palette);
        QCOMPARE(palette->property("toolTipBase").value<QColor>(), QcUiPalette::controlPalette().color(QPalette::ToolTipBase));
        QCOMPARE(palette->property("toolTipText").value<QColor>(), QcUiPalette::controlPalette().color(QPalette::ToolTipText));
    }
};

QTEST_MAIN(TestControlPalette)
#include "test_control_palette.moc"
