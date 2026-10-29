// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

#include <QPointer>

#include <memory>

using namespace Qt::StringLiterals;

// LazyScreen.qml (decision E-691): the screen exists only while shown and for its keep-alive time
// after; the clock is advanced through expireKeepAlive() instead of waiting.
class TestLazyScreen : public QObject
{
    Q_OBJECT

private:
    // A LazyScreen (in an item, as in Main.qml) whose screen counts how often it became visible
    // (onVisibleChanged); the window's root item is kept in m_root.
    // Synchronous unless `asynchronous` (the game's default) is asked for.
    QObject *create(QQmlEngine &engine, int keepAliveMs, bool asynchronous = false)
    {
        QQmlComponent component(&engine);
        const QByteArray qml = u"import QtQuick\n"
                               "Item {\n"
                               "    LazyScreen {\n"
                               "        objectName: \"lazy\"\n"
                               "        keepAliveMs: %1\n"
                               "        asynchronous: %2\n"
                               "        property int openings: 0\n"
                               "        screen: Item { objectName: \"inner\"; onVisibleChanged: if (visible) parent.parent.parent.openings++ }\n"
                               "    }\n"
                               "}\n"_s.arg(keepAliveMs)
                                   .arg(asynchronous ? u"true"_s : u"false"_s)
                                   .toUtf8();
        component.setData(qml, QUrl::fromLocalFile(QStringLiteral(QTCRAFT_QML_CONTROLS_DIR "/Test.qml")));
        m_root.reset(component.create());
        if (!m_root) {
            qWarning().noquote() << component.errorString();
            return nullptr;
        }
        return m_root->findChild<QObject *>(u"lazy"_s);
    }

    std::unique_ptr<QObject> m_root;

    static QObject *item(const QObject *lazy) { return lazy->property("item").value<QObject *>(); }

private slots:
    // Hidden: nothing is made. Shown: the screen exists at once and its onVisibleChanged ran.
    void loadsWhenShown()
    {
        QQmlEngine engine;
        QObject *lazy = create(engine, 60000);
        QVERIFY(lazy);
        QCOMPARE(item(lazy), nullptr);

        lazy->setProperty("shown", true);
        QVERIFY(item(lazy));
        QCOMPARE(item(lazy)->objectName(), u"inner"_s);
        QVERIFY(lazy->property("loaded").toBool());
        QCOMPARE(lazy->property("openings").toInt(), 1);
    }

    // Closed: kept for the keep-alive time, then unloaded; a reopen in between keeps the same
    // screen and stops the time.
    void keepsAliveThenUnloads()
    {
        QQmlEngine engine;
        QObject *lazy = create(engine, 60000);
        QVERIFY(lazy);
        lazy->setProperty("shown", true);
        QPointer<QObject> first = item(lazy);

        lazy->setProperty("shown", false);
        QVERIFY(lazy->property("keepingAlive").toBool());
        QCOMPARE(item(lazy), first.data());

        lazy->setProperty("shown", true);
        QVERIFY(!lazy->property("keepingAlive").toBool());
        QCOMPARE(item(lazy), first.data());
        QCOMPARE(lazy->property("openings").toInt(), 2);

        lazy->setProperty("shown", false);
        QVERIFY(QMetaObject::invokeMethod(lazy, "expireKeepAlive"));
        QCOMPARE(item(lazy), nullptr);
        QVERIFY(!lazy->property("loaded").toBool());
        QTRY_VERIFY(first.isNull());

        // Opened again: a new screen, which runs its opening once more.
        lazy->setProperty("shown", true);
        QVERIFY(item(lazy));
        QCOMPARE(lazy->property("openings").toInt(), 3);
    }

    // The time runs out on its own (a short one here) and does not touch a shown screen.
    void timerUnloads()
    {
        QQmlEngine engine;
        QObject *lazy = create(engine, 20);
        QVERIFY(lazy);
        lazy->setProperty("shown", true);
        QVERIFY(QMetaObject::invokeMethod(lazy, "expireKeepAlive"));
        QVERIFY(item(lazy));

        lazy->setProperty("shown", false);
        QTRY_COMPARE(item(lazy), nullptr);
    }

    // keepAliveMs 0 unloads at once.
    void zeroUnloadsAtOnce()
    {
        QQmlEngine engine;
        QObject *lazy = create(engine, 0);
        QVERIFY(lazy);
        lazy->setProperty("shown", true);
        QVERIFY(item(lazy));
        lazy->setProperty("shown", false);
        QCOMPARE(item(lazy), nullptr);
        QVERIFY(!lazy->property("keepingAlive").toBool());
    }

    // The game's screens load asynchronously (decision E-875): made over the following frames by the
    // window's incubation controller, then shown once, with their opening handler run once.
    void loadsAsynchronously()
    {
        QQmlEngine engine;
        QQuickWindow window;
        engine.setIncubationController(window.incubationController());
        QObject *lazy = create(engine, 60000, true);
        QVERIFY(lazy);
        QCOMPARE(lazy->property("asynchronous").toBool(), true);
        lazy->setProperty("shown", true);
        // Not made within the change of `shown`: pending until a later frame made it (the
        // screenshot hook waits for that).
        QVERIFY(!lazy->property("loaded").toBool());
        QVERIFY(lazy->property("screenPending").toBool());
        QTRY_VERIFY(lazy->property("loaded").toBool());
        QVERIFY(!lazy->property("screenPending").toBool());
        QCOMPARE(item(lazy)->objectName(), u"inner"_s);
        QCOMPARE(lazy->property("openings").toInt(), 1);

        // Closed and opened again within the keep-alive time: the same screen at once.
        QPointer<QObject> first = item(lazy);
        lazy->setProperty("shown", false);
        lazy->setProperty("shown", true);
        QCOMPARE(item(lazy), first.data());
        QCOMPARE(lazy->property("openings").toInt(), 2);
    }

    // Preloaded: made hidden before it is shown, its opening handler runs when it is shown, and
    // it stays while preload holds; then the keep-alive rules apply again.
    void preloadMakesItHidden()
    {
        QQmlEngine engine;
        QObject *lazy = create(engine, 0);
        QVERIFY(lazy);
        lazy->setProperty("preload", true);
        QVERIFY(item(lazy));
        QCOMPARE(lazy->property("openings").toInt(), 0);

        lazy->setProperty("shown", true);
        QCOMPARE(lazy->property("openings").toInt(), 1);
        lazy->setProperty("shown", false);
        QVERIFY(item(lazy));

        lazy->setProperty("preload", false);
        QCOMPARE(item(lazy), nullptr);
    }
};

QTEST_MAIN(TestLazyScreen)
#include "test_lazy_screen.moc"
