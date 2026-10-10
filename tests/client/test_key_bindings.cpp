#include "QcGameSettings.h"
#include "QcKeyBindings.h"
#include "QcKeyControls.h"
#include "QcTestUtil.h"

#include <QFile>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

using A = QcKeyBindings::Action;

class TestKeyBindings : public QObject
{
    Q_OBJECT

private:
    static void addPlatforms()
    {
        QTest::addColumn<QcKeyBindings::Platform>("platform");
        QTest::newRow("mac") << QcKeyBindings::Platform::Mac;
        QTest::newRow("other") << QcKeyBindings::Platform::Other;
    }

    static void writeFile(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }

private slots:
    void defaultsHaveNoConflicts_data() { addPlatforms(); }
    void defaultsHaveNoConflicts()
    {
        QFETCH(QcKeyBindings::Platform, platform);
        const QcKeyBindings defaults = QcKeyBindings::defaults(platform);
        QCOMPARE(defaults.platform(), platform);
        QVERIFY(defaults.conflicts().isEmpty());
        QSet<int> seen;
        for (const A action : QcKeyBindings::actions()) {
            const QList<int> keys = defaults.keys(action);
            for (const int key : keys) {
                QVERIFY2(!seen.contains(key), qPrintable(QcKeyBindings::id(action)));
                seen.insert(key);
            }
        }
    }

    void noDefaultPrimaryKeyIsAFunctionKey_data() { addPlatforms(); }
    void noDefaultPrimaryKeyIsAFunctionKey()
    {
        QFETCH(QcKeyBindings::Platform, platform);
        const QcKeyBindings defaults = QcKeyBindings::defaults(platform);
        for (const A action : QcKeyBindings::actions()) {
            const int primary = defaults.key(action, 0);
            QVERIFY2(primary != 0, qPrintable(QcKeyBindings::id(action)));
            QVERIFY2(!QcKeyBindings::isFunctionKey(primary), qPrintable(QcKeyBindings::id(action)));
        }
    }

    void macDefaultsAvoidCmdAndControl()
    {
        // On macOS Qt::Key_Control is Cmd and Qt::Key_Meta the Control key: Cmd+Q/H/Space and
        // Control+arrow keys/Space belong to the system.
        const QcKeyBindings defaults = QcKeyBindings::defaults(QcKeyBindings::Platform::Mac);
        for (const A action : QcKeyBindings::actions()) {
            for (int slot = 0; slot < QcKeyBindings::slotCount; ++slot) {
                const int key = defaults.key(action, slot);
                QVERIFY2(key != Qt::Key_Control && key != Qt::Key_Meta, qPrintable(QcKeyBindings::id(action)));
            }
        }

        QCOMPARE(defaults.key(A::Sprint, 0), int(Qt::Key_Alt));
        QCOMPARE(QcKeyBindings::stackModifier(QcKeyBindings::Platform::Mac), Qt::MetaModifier);
        QVERIFY(!QcKeyBindings::isBindable(Qt::Key_Control, QcKeyBindings::Platform::Mac));
        QVERIFY(QcKeyBindings::isBindable(Qt::Key_Meta, QcKeyBindings::Platform::Mac));

        QcKeyBindings keys = defaults;
        QCOMPARE(keys.bind(A::Jump, 1, Qt::Key_Control), std::nullopt);
        QCOMPARE(keys, defaults);
    }

    void macCmdPressesTriggerNothing()
    {
        // Cmd (Qt::ControlModifier on macOS) keeps Cmd+Q (quit), Cmd+H (hide), Cmd+W (close) for the
        // system; Option and Control still work with game keys.
        const QcKeyBindings mac = QcKeyBindings::defaults(QcKeyBindings::Platform::Mac);
        QCOMPARE(mac.actionForPress(Qt::Key_Q, Qt::NoModifier), std::optional<A>(A::Drop));
        QCOMPARE(mac.actionForPress(Qt::Key_Q, Qt::ControlModifier), std::nullopt);
        QCOMPARE(mac.actionForPress(Qt::Key_H, Qt::ControlModifier), std::nullopt);
        QCOMPARE(mac.actionForPress(Qt::Key_W, Qt::ControlModifier | Qt::ShiftModifier), std::nullopt);
        QCOMPARE(mac.actionForPress(Qt::Key_Q, Qt::MetaModifier), std::optional<A>(A::Drop));
        QCOMPARE(mac.actionForPress(Qt::Key_W, Qt::AltModifier), std::optional<A>(A::Forward));
        QCOMPARE(mac.actionForPress(Qt::Key_Alt, Qt::AltModifier), std::optional<A>(A::Sprint));

        // Elsewhere Ctrl is sprint and the stack modifier, so Ctrl+W and Ctrl+Q stay game keys.
        const QcKeyBindings other = QcKeyBindings::defaults(QcKeyBindings::Platform::Other);
        QCOMPARE(other.actionForPress(Qt::Key_Q, Qt::ControlModifier), std::optional<A>(A::Drop));
        QCOMPARE(other.actionForPress(Qt::Key_W, Qt::ControlModifier), std::optional<A>(A::Forward));
    }

    void otherDefaultsSprintWithCtrl()
    {
        const QcKeyBindings defaults = QcKeyBindings::defaults(QcKeyBindings::Platform::Other);
        QCOMPARE(defaults.keys(A::Sprint), QList<int>{Qt::Key_Control});
        QCOMPARE(QcKeyBindings::stackModifier(QcKeyBindings::Platform::Other), Qt::ControlModifier);
        QVERIFY(QcKeyBindings::isBindable(Qt::Key_Control, QcKeyBindings::Platform::Other));
    }

    void defaultTableAsDecided_data() { addPlatforms(); }
    void defaultTableAsDecided()
    {
        // E-077: letters for the former function-key actions, the function keys as secondary keys.
        QFETCH(QcKeyBindings::Platform, platform);
        const QcKeyBindings defaults = QcKeyBindings::defaults(platform);
        QCOMPARE(defaults.key(A::CameraView, 0), int(Qt::Key_V));
        QCOMPARE(defaults.key(A::CameraView, 1), int(Qt::Key_F5));
        QCOMPARE(defaults.key(A::Handbook, 0), int(Qt::Key_H));
        QCOMPARE(defaults.key(A::Handbook, 1), int(Qt::Key_F1));
        QCOMPARE(defaults.key(A::DebugOverlay, 0), int(Qt::Key_I));
        QCOMPARE(defaults.key(A::DebugOverlay, 1), int(Qt::Key_F3));
        // E-225: the debug options on O (options), F4 as in Factorio.
        QCOMPARE(defaults.key(A::DebugOptions, 0), int(Qt::Key_O));
        QCOMPARE(defaults.key(A::DebugOptions, 1), int(Qt::Key_F4));
        QCOMPARE(defaults.actionFor(Qt::Key_O), std::optional<A>(A::DebugOptions));
        QVERIFY(defaults.conflicts().isEmpty());
        QCOMPARE(defaults.key(A::Forward, 0), int(Qt::Key_W));
        QCOMPARE(defaults.key(A::Chat, 1), int(Qt::Key_Return));
        QCOMPARE(defaults.key(A::Hotbar9, 0), int(Qt::Key_9));
        QCOMPARE(defaults.key(A::Pause, 0), int(Qt::Key_Escape));
        QCOMPARE(defaults.actionFor(Qt::Key_F5), std::optional<A>(A::CameraView));
        QCOMPARE(defaults.actionFor(Qt::Key_Z), std::nullopt);
    }

    void idsAreUniqueAndRoundTrip()
    {
        QSet<QString> ids;
        for (const A action : QcKeyBindings::actions()) {
            const QString id = QcKeyBindings::id(action);
            QVERIFY(!ids.contains(id));
            ids.insert(id);
            QCOMPARE(QcKeyBindings::actionFromId(id), std::optional<A>(action));
        }
        QCOMPARE(ids.size(), QcKeyBindings::actionCount);
        QCOMPARE(QcKeyBindings::actionFromId(u"fly"_s), std::nullopt);
    }

    void keyNamesRoundTrip_data()
    {
        QTest::addColumn<int>("key");
        QTest::addColumn<QString>("name");
        QTest::newRow("letter") << int(Qt::Key_V) << u"V"_s;
        QTest::newRow("digit") << int(Qt::Key_7) << u"7"_s;
        QTest::newRow("F5") << int(Qt::Key_F5) << u"F5"_s;
        QTest::newRow("F12") << int(Qt::Key_F12) << u"F12"_s;
        QTest::newRow("space") << int(Qt::Key_Space) << u"Space"_s;
        QTest::newRow("shift") << int(Qt::Key_Shift) << u"Shift"_s;
        QTest::newRow("control") << int(Qt::Key_Control) << u"Control"_s;
        QTest::newRow("meta") << int(Qt::Key_Meta) << u"Meta"_s;
        QTest::newRow("return") << int(Qt::Key_Return) << u"Return"_s;
        QTest::newRow("slash") << int(Qt::Key_Slash) << u"/"_s;
        QTest::newRow("comma") << int(Qt::Key_Comma) << u","_s;
        QTest::newRow("semicolon") << int(Qt::Key_Semicolon) << u";"_s;
        QTest::newRow("umlaut") << int(Qt::Key_Odiaeresis) << u"Ö"_s;
        QTest::newRow("sharp s") << int(Qt::Key_ssharp) << u"ß"_s;
        QTest::newRow("up") << int(Qt::Key_Up) << u"Up"_s;
    }

    void keyNamesRoundTrip()
    {
        QFETCH(int, key);
        QFETCH(QString, name);
        QCOMPARE(QcKeyBindings::keyName(key), name);
        QCOMPARE(QcKeyBindings::keyFromName(name), std::optional<int>(key));
    }

    void badKeyNames()
    {
        QCOMPARE(QcKeyBindings::keyFromName(u""_s), std::optional<int>(0));
        QCOMPARE(QcKeyBindings::keyFromName(u"v"_s), std::optional<int>(int(Qt::Key_V)));
        QCOMPARE(QcKeyBindings::keyFromName(u"F0"_s), std::nullopt);
        QCOMPARE(QcKeyBindings::keyFromName(u"F36"_s), std::nullopt);
        QCOMPARE(QcKeyBindings::keyFromName(u"Fx"_s), std::nullopt);
        QCOMPARE(QcKeyBindings::keyFromName(u"Banana"_s), std::nullopt);
        QCOMPARE(QcKeyBindings::keyName(0), QString());
        QCOMPARE(QcKeyBindings::keyName(Qt::Key_unknown), QString());
        const auto platform = QcKeyBindings::Platform::Other;
        QVERIFY(!QcKeyBindings::isBindable(Qt::Key_Escape, platform));
        QVERIFY(!QcKeyBindings::isBindable(Qt::Key_Backspace, platform));
        QVERIFY(QcKeyBindings::isBindable(Qt::Key_F5, platform));
    }

    void bindingAFreeKey()
    {
        QcKeyBindings keys = QcKeyBindings::defaults();
        QCOMPARE(keys.bind(A::CameraView, 0, Qt::Key_C), std::nullopt);
        QCOMPARE(keys.key(A::CameraView, 0), int(Qt::Key_C));
        QVERIFY(keys.matches(A::CameraView, Qt::Key_F5));
        QVERIFY(!keys.matches(A::CameraView, Qt::Key_V));
        QCOMPARE(keys.actionFor(Qt::Key_V), std::nullopt);
        QVERIFY(keys.conflicts().isEmpty());
    }

    void bindingAUsedKeySwaps()
    {
        QcKeyBindings keys = QcKeyBindings::defaults();
        // E belongs to the inventory; the camera view had V, so the inventory gets V.
        const std::optional<QcKeyBindings::Displaced> displaced = keys.bind(A::CameraView, 0, Qt::Key_E);
        QVERIFY(displaced.has_value());
        QCOMPARE(displaced->action, A::Inventory);
        QCOMPARE(displaced->slot, 0);
        QCOMPARE(displaced->key, int(Qt::Key_V));
        QCOMPARE(keys.key(A::CameraView, 0), int(Qt::Key_E));
        QCOMPARE(keys.key(A::Inventory, 0), int(Qt::Key_V));
        QVERIFY(keys.conflicts().isEmpty());
    }

    void bindingIntoAnEmptySlotLeavesTheOtherWithout()
    {
        QcKeyBindings keys = QcKeyBindings::defaults();
        // The jump has no secondary key; taking W from walking forward leaves that slot empty.
        const std::optional<QcKeyBindings::Displaced> displaced = keys.bind(A::Jump, 1, Qt::Key_W);
        QVERIFY(displaced.has_value());
        QCOMPARE(displaced->action, A::Forward);
        QCOMPARE(displaced->key, 0);
        QCOMPARE(keys.key(A::Forward, 0), 0);
        QVERIFY(keys.matches(A::Jump, Qt::Key_W));
        QVERIFY(keys.conflicts().isEmpty());
    }

    void movingAKeyWithinOneAction()
    {
        QcKeyBindings keys = QcKeyBindings::defaults();
        keys.bind(A::CameraView, 1, Qt::Key_V);
        QCOMPARE(keys.key(A::CameraView, 1), int(Qt::Key_V));
        QCOMPARE(keys.key(A::CameraView, 0), int(Qt::Key_F5));
        QVERIFY(keys.conflicts().isEmpty());
    }

    void refusedBindingsChangeNothing()
    {
        QcKeyBindings keys = QcKeyBindings::defaults(QcKeyBindings::Platform::Other);
        QCOMPARE(keys.bind(A::Pause, 0, Qt::Key_P), std::nullopt);
        QCOMPARE(keys.bind(A::Jump, 0, Qt::Key_Escape), std::nullopt);
        QCOMPARE(keys.bind(A::Jump, 2, Qt::Key_J), std::nullopt);
        QCOMPARE(keys, QcKeyBindings::defaults(QcKeyBindings::Platform::Other));
        keys.bind(A::Chat, 1, 0);
        QCOMPARE(keys.keys(A::Chat), QList<int>{Qt::Key_T});
    }

    void placeholdersNameTheKeys()
    {
        const QMap<QString, QStringList> values = QcKeyBindings::defaults(QcKeyBindings::Platform::Other).placeholderKeys();
        QCOMPARE(values.value(u"key.cameraView"_s), (QStringList{u"V"_s, u"F5"_s}));
        QCOMPARE(values.value(u"key1.cameraView"_s), QStringList{u"V"_s});
        QCOMPARE(values.value(u"key.pause"_s), QStringList{u"Esc"_s});
        QCOMPARE(values.value(u"key.sprint"_s), QStringList{u"Control"_s});
        // On a Mac the modifiers are named like its keys.
        QcKeyBindings mac = QcKeyBindings::defaults(QcKeyBindings::Platform::Mac);
        mac.bind(A::Sneak, 1, Qt::Key_Meta);
        const QMap<QString, QStringList> macValues = mac.placeholderKeys();
        QCOMPARE(macValues.value(u"key.sprint"_s), QStringList{u"MacOption"_s});
        QCOMPARE(macValues.value(u"key.sneak"_s), (QStringList{u"Shift"_s, u"MacControl"_s}));
    }

    void settingsRoundTrip()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"settings.ini"_s);
        QcGameSettings settings;
        settings.keys.bind(A::CameraView, 0, Qt::Key_C);
        settings.keys.bind(A::Command, 0, Qt::Key_Comma);
        settings.keys.bind(A::Drop, 1, Qt::Key_Semicolon);
        settings.keys.bind(A::Handbook, 1, 0);
        settings.keys.bind(A::Jump, 1, Qt::Key_Odiaeresis);
        const QcStatus saved = settings.save(path);
        QVERIFY2(saved.has_value(), qcErrorText(saved));
        const QcGameSettings loaded = QcGameSettings::load(path);
        QCOMPARE(loaded.keys, settings.keys);
        QCOMPARE(loaded.keys.key(A::Handbook, 1), 0);
    }

    void invalidEntriesFallBack()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"settings.ini"_s);
        // Unknown names, Esc and an unknown action fall back or are ignored; "" clears the slot.
        writeFile(path, "[keys]\nforward=Banana\njump=Esc\ncameraView=B\ncameraView.2=\npause=P\nfly=G\nhandbook=F99\n");
        const QcKeyBindings keys = QcGameSettings::load(path).keys;
        const QcKeyBindings defaults = QcKeyBindings::defaults();
        QCOMPARE(keys.key(A::Forward, 0), defaults.key(A::Forward, 0));
        QCOMPARE(keys.key(A::Jump, 0), defaults.key(A::Jump, 0));
        QCOMPARE(keys.key(A::CameraView, 0), int(Qt::Key_B));
        QCOMPARE(keys.key(A::CameraView, 1), 0);
        QCOMPARE(keys.key(A::Pause, 0), int(Qt::Key_Escape));
        QCOMPARE(keys.key(A::Handbook, 0), int(Qt::Key_H));
    }

    void duplicateEntriesAreDropped()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"settings.ini"_s);
        // Both chosen by the player: the inventory comes before the handbook in the table and keeps E.
        writeFile(path, "[keys]\nhandbook=E\ninventory=E\n");
        const QcKeyBindings keys = QcGameSettings::load(path).keys;
        QVERIFY(keys.conflicts().isEmpty());
        QCOMPARE(keys.key(A::Inventory, 0), int(Qt::Key_E));
        QCOMPARE(keys.key(A::Handbook, 0), 0);
        QCOMPARE(keys.key(A::Handbook, 1), int(Qt::Key_F1));
    }

    void playersKeyBeatsANewDefault()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"settings.ini"_s);
        // Labels on O, set before the debug options got O as their default (stage 21b): the player's
        // choice stays, the debug options keep only F4.
        writeFile(path, "[keys]\nlabels=O\n");
        QTest::ignoreMessage(QtWarningMsg, "Key O of 'debugOptions' is already bound to another action; 'debugOptions' goes without it");
        const QcKeyBindings keys = QcGameSettings::load(path).keys;
        QVERIFY(keys.conflicts().isEmpty());
        QCOMPARE(keys.key(A::Labels, 0), int(Qt::Key_O));
        QCOMPARE(keys.keys(A::DebugOptions), QList<int>{int(Qt::Key_F4)});
        QCOMPARE(keys.actionFor(Qt::Key_O), std::optional(A::Labels));
    }

    void macSettingsRefuseCmd()
    {
        if (QcKeyBindings::currentPlatform() != QcKeyBindings::Platform::Mac) {
            QSKIP("Only a Mac refuses Cmd");
        }
        QTemporaryDir dir;
        const QString path = dir.filePath(u"settings.ini"_s);
        writeFile(path, "[keys]\nsprint=Control\njump.2=Meta\n");
        const QcKeyBindings keys = QcGameSettings::load(path).keys;
        QCOMPARE(keys.key(A::Sprint, 0), int(Qt::Key_Alt));
        QCOMPARE(keys.key(A::Jump, 1), int(Qt::Key_Meta));
    }

    void controlsBindClearsSwapsAndRefuses()
    {
        QcKeyControls controls;
        QSignalSpy edited(&controls, &QcKeyControls::edited);
        QSignalSpy changed(&controls, &QcKeyControls::changed);

        // Backspace clears the slot.
        QCOMPARE(controls.bind(u"cameraView"_s, 1, Qt::Key_Backspace), QString());
        QCOMPARE(controls.bindings().key(A::CameraView, 1), 0);
        QCOMPARE(edited.count(), 1);
        QCOMPARE(changed.count(), 1);

        // Esc is refused with a message and changes nothing.
        QVERIFY(!controls.bind(u"jump"_s, 0, Qt::Key_Escape).isEmpty());
        QCOMPARE(edited.count(), 1);

        // A used key is swapped and the message names the other action.
        const QString swapped = controls.bind(u"cameraView"_s, 0, Qt::Key_E);
        QVERIFY2(swapped.contains(u"Inventory"_s), qPrintable(swapped));
        QCOMPARE(controls.bindings().key(A::Inventory, 0), int(Qt::Key_V));
        QCOMPARE(edited.count(), 2);

        // The same key again, an unknown action and the fixed pause key change nothing.
        QCOMPARE(controls.bind(u"cameraView"_s, 0, Qt::Key_E), QString());
        QCOMPARE(controls.bind(u"fly"_s, 0, Qt::Key_G), QString());
        QCOMPARE(controls.bind(u"pause"_s, 0, Qt::Key_P), QString());
        QCOMPARE(edited.count(), 2);
        QVERIFY(controls.matches(u"cameraView"_s, Qt::Key_E));
        QCOMPARE(controls.labels().value(u"inventory"_s).toString(), u"V"_s);
        QCOMPARE(controls.rows().size(), QcKeyBindings::actionCount);
        controls.reset();
        QCOMPARE(controls.bindings(), QcKeyBindings::defaults());
        QCOMPARE(edited.count(), 3);
        controls.reset();
        QCOMPARE(edited.count(), 3);
    }

    void controlsRefuseCmdOnAMac()
    {
        QcKeyControls controls;
        controls.setBindings(QcKeyBindings::defaults(QcKeyBindings::Platform::Mac));
        QSignalSpy edited(&controls, &QcKeyControls::edited);
        QVERIFY(controls.bind(u"sprint"_s, 0, Qt::Key_Control).contains(u"Cmd"_s));
        QCOMPARE(edited.count(), 0);
        // Control is allowed with a warning about the system's Control shortcuts.
        QVERIFY(controls.bind(u"sprint"_s, 1, Qt::Key_Meta).contains(u"Control"_s));
        QCOMPARE(edited.count(), 1);
    }
};

QTEST_GUILESS_MAIN(TestKeyBindings)
#include "test_key_bindings.moc"
