#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
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
    QObject *create(QQmlEngine &engine, int keepAliveMs)
    {
        QQmlComponent component(&engine);
        const QByteArray qml = u"import QtQuick\n"
                               "Item {\n"
                               "    LazyScreen {\n"
                               "        objectName: \"lazy\"\n"
                               "        keepAliveMs: %1\n"
                               "        property int openings: 0\n"
                               "        screen: Item { objectName: \"inner\"; onVisibleChanged: if (visible) parent.parent.parent.openings++ }\n"
                               "    }\n"
                               "}\n"_s.arg(keepAliveMs)
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
