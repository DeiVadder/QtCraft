#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

// The game's keyboard over a window with an address field and a password field.
const char *const sceneQml = R"(
import QtQuick
import QtQuick.Controls
import QtCraft

ApplicationWindow {
    width: 900
    height: 600
    visible: true

    TextField { objectName: "address"; width: 300 }
    TextField { objectName: "password"; y: 50; width: 300; echoMode: TextInput.Password }

    OnScreenKeyboard {
        objectName: "keyboard"
        areaWidth: 900
        areaHeight: 600
    }
}
)";

} // namespace

// The on-screen keyboard of the pad (decision E-584), clicked key by key: addresses with a port and
// IPv6 ("host:25565", "[::1]:25565"), the second character of each key under shift, umlauts, and a
// password field whose preview (and thereby the scripted pad's log) shows only the field's dots.
class TestOnScreenKeyboard : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QQuickWindow *m_window = nullptr;

    // Through the item tree: the keys come from repeaters and have no object parent.
    static QQuickItem *find(QQuickItem *root, const QString &name)
    {
        if (root->objectName() == name) {
            return root;
        }
        const QList<QQuickItem *> children = root->childItems();
        for (QQuickItem *child : children) {
            if (QQuickItem *found = find(child, name)) {
                return found;
            }
        }
        return nullptr;
    }

    QQuickItem *item(const QString &name) const { return find(m_window->contentItem(), name); }
    QObject *keyboard() const { return m_window->findChild<QObject *>(u"keyboard"_s); }

    void openFor(const QString &field)
    {
        QVERIFY(QMetaObject::invokeMethod(keyboard(), "openFor", Q_ARG(QQuickItem*, item(field))));
        QTRY_VERIFY(keyboard()->property("opened").toBool());
    }

    void press(const QString &name)
    {
        QQuickItem *key = item(name);
        QVERIFY2(key && key->isVisible(), qPrintable(name));
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, key->mapToScene(QPointF(key->width() / 2, key->height() / 2)).toPoint());
    }

    // Clicks the keys for `text`, with shift for the second characters.
    void type(const QString &text)
    {
        const QVariantList rows = keyboard()->property("rows").toList();
        for (const QChar character : text) {
            if (character == u' ') {
                press(u"keySpace"_s);
                continue;
            }
            bool found = false;
            for (const QVariant &row : rows) {
                const QVariantList keys = row.toList();
                for (const QVariant &key : keys) {
                    const QString pair = key.toString();
                    if (found || !pair.contains(character)) {
                        continue;
                    }
                    found = true;
                    if (pair.at(0) != character) {
                        press(u"keyShift"_s);
                    }
                    press(u"key_"_s + pair.at(0));
                }
            }
            QVERIFY2(found, qPrintable(QString(character)));
        }
    }

    QString text(const QString &field) const { return item(field)->property("text").toString(); }

private slots:
    void initTestCase() { QQuickStyle::setStyle(u"Fusion"_s); }

    void init()
    {
        m_engine = std::make_unique<QQmlApplicationEngine>();
        m_engine->loadData(sceneQml);
        QVERIFY(!m_engine->rootObjects().isEmpty());
        m_window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().first());
        QVERIFY(m_window);
        QVERIFY(QTest::qWaitForWindowExposed(m_window));
    }

    void cleanup()
    {
        m_engine.reset();
        m_window = nullptr;
    }

    void typesAddressesWithPorts()
    {
        openFor(u"address"_s);
        type(u"host:25565"_s);
        QCOMPARE(text(u"address"_s), u"host:25565"_s);

        item(u"address"_s)->setProperty("text", QString());
        type(u"[::1]:25565"_s);
        QCOMPARE(text(u"address"_s), u"[::1]:25565"_s);
        QCOMPARE(item(u"keyboardPreview"_s)->property("text").toString(), u"[::1]:25565"_s);
    }

    // Shift gives one second character (capitals, symbols) and is let go after it.
    void shiftTypesTheSecondCharacter()
    {
        openFor(u"address"_s);
        type(u"Hi!_?@ äöüß Ä"_s);
        QCOMPARE(text(u"address"_s), u"Hi!_?@ äöüß Ä"_s);
        QVERIFY(!keyboard()->property("shifted").toBool());
    }

    // The preview shows what the field shows: a password's dots, never its text.
    void passwordStaysHidden()
    {
        openFor(u"password"_s);
        type(u"secret"_s);
        QCOMPARE(text(u"password"_s), u"secret"_s);
        const QString preview = item(u"keyboardPreview"_s)->property("text").toString();
        QCOMPARE(preview.size(), 6);
        QVERIFY(!preview.contains(u"secret"_s));
        QCOMPARE(preview, item(u"password"_s)->property("displayText").toString());
    }
};

QTEST_MAIN(TestOnScreenKeyboard)
#include "test_onscreen_keyboard.moc"
