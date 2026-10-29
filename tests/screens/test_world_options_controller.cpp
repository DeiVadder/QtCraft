// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/controllers/QcWorldLibraryController.h"
#include "app/controllers/QcWorldOptionsController.h"
#include "common/content/registry/QcContentRegistry.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

// The menu's world options (GameController.worldOptions, decision E-917): the values a slider sends
// land on the option's grid, so that moving back to the default counts as no change, and the
// controller lets go of the menu's content when a world starts.
class TestWorldOptionsController : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_data.isValid());
        qputenv("QTCRAFT_DATA_DIR", m_data.path().toUtf8());
    }

    // A slider sends 0.6000000000000001 for 0.6: still the default, and stored as 0.6.
    void aSliderBackAtTheDefaultChangesNothing()
    {
        Fixture fixture;
        QcWorldOptionsController &options = fixture.options;
        options.reset();
        QCOMPARE(options.changedCount(), 0);
        const double dungeons = options.values().value(u"qtcraft:dungeons"_s).toDouble();
        QCOMPARE(dungeons, 0.6);

        options.setValue(u"qtcraft:dungeons"_s, 0.65 - 0.05);
        QCOMPARE(options.changedCount(), 0);
        QCOMPARE(options.values().value(u"qtcraft:dungeons"_s).toDouble(), 0.6);
        QCOMPARE(options.preset(), QString());

        options.setValue(u"qtcraft:dungeons"_s, 0.65);
        QCOMPARE(options.changedCount(), 1);
        options.setValue(u"qtcraft:dungeons"_s, 0.7 - 0.1);
        QCOMPARE(options.changedCount(), 0);
    }

    // Every slider gets its default to mark on the track (decision E-930): the value a reset sets.
    void everyNumberOptionNamesItsDefault()
    {
        Fixture fixture;
        QcWorldOptionsController &options = fixture.options;
        options.reset();
        int numbers = 0;
        const QVariantList groups = options.groups();
        for (const QVariant &group : groups) {
            const QVariantList entries = group.toMap().value(u"options"_s).toList();
            for (const QVariant &entry : entries) {
                const QVariantMap option = entry.toMap();
                if (option.value(u"isSwitch"_s).toBool()) {
                    continue;
                }
                const QString name = option.value(u"name"_s).toString();
                QVERIFY2(option.contains(u"defaultValue"_s), qPrintable(name));
                QCOMPARE(option.value(u"defaultValue"_s).toDouble(), options.values().value(name).toDouble());
                QVERIFY(option.value(u"defaultValue"_s).toDouble() >= option.value(u"from"_s).toDouble());
                QVERIFY(option.value(u"defaultValue"_s).toDouble() <= option.value(u"to"_s).toDouble());
                ++numbers;
            }
        }
        QVERIFY(numbers >= 5);
        QCOMPARE(options.values().value(u"qtcraft:village_spacing"_s).toDouble(), 384.0);
    }

    void numbersSnapToTheOptionsGrid()
    {
        Fixture fixture;
        QcWorldOptionsController &options = fixture.options;
        options.reset();

        options.setValue(u"qtcraft:biome_size"_s, 2.9999999999999996);
        QCOMPARE(options.values().value(u"qtcraft:biome_size"_s).toDouble(), 3.0);
        QCOMPARE(options.changedCount(), 1);
        options.setValue(u"qtcraft:biome_size"_s, 2.0000000000000004);
        QCOMPARE(options.preset(), u"qtcraft:large_biomes"_s);
        options.setValue(u"qtcraft:biome_size"_s, 1.0000000000000002);
        QCOMPARE(options.changedCount(), 0);

        options.setValue(u"qtcraft:village_spacing"_s, 250.0);
        QCOMPARE(options.values().value(u"qtcraft:village_spacing"_s).toDouble(), 256.0);

        // Outside the range, unknown or of the wrong kind: refused, the value stays.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"World option qtcraft:village_spacing refused"_s));
        options.setValue(u"qtcraft:village_spacing"_s, 5000.0);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"World option qtcraft:caves refused"_s));
        options.setValue(u"qtcraft:caves"_s, 1.0);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"World option qtcraft:lakes refused"_s));
        options.setValue(u"qtcraft:lakes"_s, true);
        QCOMPARE(options.values().value(u"qtcraft:village_spacing"_s).toDouble(), 256.0);
        QCOMPARE(options.values().value(u"qtcraft:caves"_s).toBool(), true);
    }

    // Once a world starts, the menu's content is no longer held; the next form loads the content
    // the library has then.
    void aStartedWorldReleasesTheMenusContent()
    {
        Fixture fixture;
        QcWorldOptionsController &options = fixture.options;
        options.reset();
        QVERIFY(!options.groups().isEmpty());
        const std::weak_ptr<const QcContentRegistry> menu = fixture.library.loadedContent();
        QVERIFY(!menu.expired());

        fixture.library.useContent(std::make_shared<const QcContentRegistry>());
        QVERIFY(!menu.expired());
        options.release();
        QVERIFY(menu.expired());
        QCOMPARE(options.changedCount(), 0);

        // The library's content changed: the next form takes the options of the new content.
        options.reset();
        QVERIFY(options.groups().isEmpty());
        QCOMPARE(options.values(), QVariantMap());
    }

private:
    struct Fixture
    {
        Fixture() { library.setModsDirectory({}); }

        QcWorldLibraryController library;
        QcWorldOptionsController options{library, [] { return u"en"_s; }};
    };

    QTemporaryDir m_data;
};

QTEST_GUILESS_MAIN(TestWorldOptionsController)
#include "test_world_options_controller.moc"
