// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/session/QcLoadingProgress.h"
#include "clientcore/settings/QcKeyControls.h"

#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;

namespace {

// The message screen (as in the Error phase) and the loading screen of Screens.qml, each over
// the whole window and counting the ways back it asks for. `wayBack` is the message's back button
// and whether the loading can be cancelled (a network game's).
const char *const sceneQml = R"(
import QtQuick
import QtQuick.Controls
import QtCraft

ApplicationWindow {
    id: window

    required property KeyControls keys
    required property LoadingProgress progress
    required property bool loading
    required property bool wayBack
    property int backs: 0
    property int cancels: 0

    width: 1024
    height: 640
    visible: true

    MessageScreen {
        anchors.fill: parent
        visible: !window.loading
        keys: window.keys
        text: "The world could not be loaded"
        showBackButton: window.wayBack
        onBackRequested: window.backs++
    }

    LoadingScreen {
        anchors.fill: parent
        visible: window.loading
        focus: visible
        progress: window.progress
        keys: window.keys
        canCancel: window.wayBack
        onCancelRequested: window.cancels++
    }
}
)";

} // namespace

// Esc on the screens without a world (row 79.76): the message screen of a world that could not
// start goes back to the menu, the loading screen of a network game cancels; without a way back
// (no back button, a local world's loading) Esc does nothing.
class TestScreenEscape : public QObject
{
    Q_OBJECT

    QcKeyControls m_keys;
    QcLoadingProgress m_progress;
    std::unique_ptr<QQmlApplicationEngine> m_engine;

    // The scene shown with the message (`loading` false) or the loading screen; `wayBack` as above.
    QQuickWindow *show(bool loading, bool wayBack)
    {
        m_engine = std::make_unique<QQmlApplicationEngine>();
        m_engine->setInitialProperties(
            {{u"keys"_s, QVariant::fromValue(&m_keys)}, {u"progress"_s, QVariant::fromValue(&m_progress)}, {u"loading"_s, loading}, {u"wayBack"_s, wayBack}});
        m_engine->loadData(sceneQml);
        if (m_engine->rootObjects().isEmpty()) {
            return nullptr;
        }
        auto *window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().constFirst());
        if (window && QTest::qWaitForWindowExposed(window)) {
            return window;
        }
        return nullptr;
    }

private slots:
    void initTestCase() { QQuickStyle::setStyle(u"Basic"_s); }
    void cleanup() { m_engine.reset(); }

    void escapeOnTheMessageGoesBack()
    {
        QQuickWindow *window = show(false, true);
        QVERIFY(window);
        QTest::keyClick(window, Qt::Key_Escape);
        QCOMPARE(window->property("backs").toInt(), 1);
    }

    void escapeOnAMessageWithoutWayBackDoesNothing()
    {
        QQuickWindow *window = show(false, false);
        QVERIFY(window);
        QTest::keyClick(window, Qt::Key_Escape);
        QCOMPARE(window->property("backs").toInt(), 0);
    }

    void escapeCancelsANetworkGamesLoading()
    {
        QQuickWindow *window = show(true, true);
        QVERIFY(window);
        QTest::keyClick(window, Qt::Key_Escape);
        QCOMPARE(window->property("cancels").toInt(), 1);
        // Another key does not cancel.
        QTest::keyClick(window, Qt::Key_A);
        QCOMPARE(window->property("cancels").toInt(), 1);
    }

    void escapeOnALocalLoadingDoesNothing()
    {
        QQuickWindow *window = show(true, false);
        QVERIFY(window);
        QTest::keyClick(window, Qt::Key_Escape);
        QCOMPARE(window->property("cancels").toInt(), 0);
    }
};

QTEST_MAIN(TestScreenEscape)
#include "test_screen_escape.moc"
