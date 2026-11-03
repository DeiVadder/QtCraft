// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/controllers/QcNewWorldDraft.h"
#include "app/controllers/QcWorldLibraryController.h"
#include "app/controllers/QcWorldOptionsController.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/world/QcGeneratorVersion.h"

#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

// The menu's world options (GameController.worldOptions, decision E-917): the values a slider sends
// land on the option's grid, so that moving back to the default counts as no change, and the
// controller lets go of the menu's content when a world starts. The rest of the new world's form
// (QcNewWorldDraft, decision E-956): defaults, the random seed, the generator version, refused
// values and what createWorld gets.
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

    // The preset list's entry and the form's line follow the values (and the presets).
    void presetIndexAndSummaryFollowTheValues()
    {
        Fixture fixture;
        QcWorldOptionsController &options = fixture.options;
        QSignalSpy summaries(&options, &QcWorldOptionsController::summaryChanged);
        options.reset();
        QVERIFY(summaries.count() >= 1);
        QCOMPARE(options.presetIndex(), 0);
        QCOMPARE(options.summary(), u"Every option at its default."_s);

        options.choosePreset(u"qtcraft:large_biomes"_s);
        const QVariantList presets = options.presets();
        QVERIFY(options.presetIndex() > 0);
        const QVariantMap chosen = presets.at(options.presetIndex() - 1).toMap();
        QCOMPARE(chosen.value(u"name"_s).toString(), u"qtcraft:large_biomes"_s);
        QCOMPARE(options.summary(), u"Preset: %1"_s.arg(chosen.value(u"title"_s).toString()));

        options.setValue(u"qtcraft:dungeons"_s, 0.2);
        QCOMPARE(options.presetIndex(), -1);
        QCOMPARE(options.summary(), QcWorldOptionsController::tr("%n option(s) changed.", nullptr, 2));
        options.choosePreset({});
        QCOMPARE(options.presetIndex(), 0);
    }

    // The form opens with its defaults and a random seed the map shows; the field's text, trimmed,
    // wins over it. The die puts a new one into the field.
    void theDraftStartsWithItsDefaultsAndARandomSeed()
    {
        Fixture fixture;
        QcNewWorldDraft &draft = *fixture.options.draft();
        draft.setName(u"Old"_s);
        draft.setWorldType(QcNewWorldDraft::WorldType::Flat);
        draft.setLayers(u"stone"_s);
        draft.setKeepInventory(false);
        draft.setGameMode(QcGameMode::Creative);
        draft.setSeedText(u"42"_s);
        draft.reset();
        QCOMPARE(draft.name(), u"New World"_s);
        QCOMPARE(draft.worldType(), QcNewWorldDraft::WorldType::Terrain);
        QVERIFY(draft.terrain());
        QCOMPARE(draft.layers(), QString());
        QVERIFY(draft.keepInventory());
        QCOMPARE(draft.gameMode(), QcGameMode::Survival);
        QCOMPARE(draft.seedText(), QString());
        bool number = false;
        const qint64 random = draft.randomSeed().toLongLong(&number);
        QVERIFY(number);
        QVERIFY(random >= 0 && random < QcNewWorldDraft::maxRandomSeed);
        QCOMPARE(draft.seed(), draft.randomSeed());
        QCOMPARE(fixture.options.preview()->seedText(), draft.seed());

        draft.setSeedText(u"  island  "_s);
        QCOMPARE(draft.seed(), u"island"_s);
        QCOMPARE(fixture.options.preview()->seedText(), u"island"_s);
        draft.setSeedText(u"   "_s);
        QCOMPARE(draft.seed(), draft.randomSeed());

        draft.rollSeed();
        QVERIFY(!draft.seedText().isEmpty());
        QCOMPARE(draft.seedText(), draft.randomSeed());
        QCOMPARE(draft.seed(), draft.seedText());
        for (int round = 0; round < 50; ++round) {
            const qint64 drawn = QcNewWorldDraft::drawSeed().toLongLong(&number);
            QVERIFY(number && drawn >= 0 && drawn < QcNewWorldDraft::maxRandomSeed);
        }
    }

    // A type or mode that is none is refused with a warning; the start choices of the test hooks
    // hold for every reset. setup() is what createWorld takes.
    void theDraftRefusesUnknownChoicesAndMakesTheSetup()
    {
        Fixture fixture;
        QcNewWorldDraft &draft = *fixture.options.draft();
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"New world: refused the world type 7"_s));
        draft.setWorldType(static_cast<QcNewWorldDraft::WorldType>(7));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"New world: refused the game mode 9"_s));
        draft.setGameMode(static_cast<QcGameMode>(9));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"New world: refused the start world type -1"_s));
        draft.setStartWorldType(static_cast<QcNewWorldDraft::WorldType>(-1));
        QCOMPARE(draft.worldType(), QcNewWorldDraft::WorldType::Terrain);
        QCOMPARE(draft.gameMode(), QcGameMode::Survival);

        draft.setStartWorldType(QcNewWorldDraft::WorldType::Flat);
        draft.setStartGameMode(QcGameMode::Adventure);
        draft.reset();
        QCOMPARE(draft.worldType(), QcNewWorldDraft::WorldType::Flat);
        QCOMPARE(draft.gameMode(), QcGameMode::Adventure);
        QVERIFY(!draft.terrain());

        draft.setLayers(u"stone,dirt"_s);
        draft.setKeepInventory(false);
        QVariantMap setup = draft.setup();
        QCOMPARE(setup.value(u"worldType"_s).toInt(), static_cast<int>(QcNewWorldDraft::WorldType::Flat));
        QCOMPARE(setup.value(u"gameMode"_s).toInt(), static_cast<int>(QcGameMode::Adventure));
        QCOMPARE(setup.value(u"layers"_s).toString(), u"stone,dirt"_s);
        QCOMPARE(setup.value(u"keepInventory"_s).toBool(), false);
        QCOMPARE(setup.value(u"worldOptions"_s).toMap(), fixture.options.values());

        // The layers count only for a flat world.
        draft.setWorldType(QcNewWorldDraft::WorldType::Void);
        setup = draft.setup();
        QCOMPARE(setup.value(u"layers"_s).toString(), QString());
    }

    // The generator version (decision E-922): the current one by default and after every reset,
    // the versions newest first; a chosen one goes into the setup and the map preview, a version
    // the game does not have is refused with a warning.
    void theDraftChoosesTheGeneratorVersion()
    {
        Fixture fixture;
        QcNewWorldDraft &draft = *fixture.options.draft();
        QCOMPARE(draft.generatorVersion(), QcGeneratorVersion::current);
        QCOMPARE(QcNewWorldDraft::currentGeneratorVersion(), QcGeneratorVersion::current);
        const QList<int> versions = QcNewWorldDraft::generatorVersions();
        QCOMPARE(versions.size(), QcGeneratorVersion::current - QcGeneratorVersion::oldest + 1);
        QCOMPARE(versions.constFirst(), QcGeneratorVersion::current);
        QCOMPARE(versions.constLast(), QcGeneratorVersion::oldest);
        QCOMPARE(draft.setup().value(u"generatorVersion"_s).toInt(), QcGeneratorVersion::current);
        QCOMPARE(fixture.options.preview()->generatorVersion(), QcGeneratorVersion::current);

        QSignalSpy changed(&draft, &QcNewWorldDraft::generatorVersionChanged);
        draft.setGeneratorVersion(QcGeneratorVersion::oldest);
        QCOMPARE(changed.count(), 1);
        draft.setGeneratorVersion(QcGeneratorVersion::oldest);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(draft.setup().value(u"generatorVersion"_s).toInt(), QcGeneratorVersion::oldest);
        QCOMPARE(fixture.options.preview()->generatorVersion(), QcGeneratorVersion::oldest);

        for (const int refused : {QcGeneratorVersion::oldest - 1, QcGeneratorVersion::current + 1}) {
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"New world: refused the world generator version %1"_s.arg(refused)));
            draft.setGeneratorVersion(refused);
        }
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Map preview: refused the world generator version 0"_s));
        fixture.options.preview()->setGeneratorVersion(0);
        QCOMPARE(draft.generatorVersion(), QcGeneratorVersion::oldest);
        QCOMPARE(fixture.options.preview()->generatorVersion(), QcGeneratorVersion::oldest);

        draft.reset();
        QCOMPARE(draft.generatorVersion(), QcGeneratorVersion::current);
        QCOMPARE(fixture.options.preview()->generatorVersion(), QcGeneratorVersion::current);
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
