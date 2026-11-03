// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/controllers/QcGameController.h"
#include "app/controllers/QcNewWorldDraft.h"
#include "app/controllers/QcWorldLibraryController.h"
#include "app/controllers/QcWorldOptionsController.h"
#include "app/hooks/QcMenuPreselect.h"
#include "app/models/QcHandbookModel.h"
#include "app/session/QcSessionParts.h"
#include "app/settings/QcSettingsController.h"
#include "audio/QcAudioEngine.h"
#include "clientcore/meshing/QcMeshScheduler.h"
#include "clientcore/session/QcClientSession.h"
#include "clientcore/texturepacks/QcTexturePack.h"
#include "common/gameplay/QcGameMode.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worlds/QcLocalServerHost.h"
#include "worlds/QcModLoader.h"
#include "worlds/QcModStatus.h"
#include "worlds/QcWorldLibrary.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaEnum>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <memory>

using namespace Qt::StringLiterals;

namespace {

bool writeText(const QString &path, const QByteArray &text)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(text) == text.size();
}

// The value of `key` in each map of `list`, by the map's `by`.
QHash<QString, int> valuesBy(const QVariantList &list, const QString &by, const QString &key)
{
    QHash<QString, int> values;
    for (const QVariant &entry : list) {
        values.insert(entry.toMap().value(by).toString(), entry.toMap().value(key).toInt());
    }
    return values;
}

} // namespace

// The modes, kinds and states the menus compare as enumerations instead of strings (decision
// E-688, package 79.62): the game mode and world type of the world list and of a new world, the
// state of a mod, the origin of a texture pack, the kind of a handbook recipe and the on-screen
// keyboard's layout; QML knows each under its name.
class TestMenuEnums : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_data.isValid());
        qputenv("QTCRAFT_DATA_DIR", m_data.path().toUtf8());
    }

    // Every world type has its generator's name and comes back from it; an unknown or missing name
    // is a terrain world, as world.json without a type is.
    void worldTypesAreTheGenerators()
    {
        using Type = QcWorldLibraryController::WorldType;
        QCOMPARE(QcWorldLibraryController::generatorOf(Type::Terrain), QLatin1StringView(QcWorldGenerators::terrain));
        QCOMPARE(QcWorldLibraryController::generatorOf(Type::Flat), QLatin1StringView(QcWorldGenerators::flat));
        QCOMPARE(QcWorldLibraryController::generatorOf(Type::Void), QLatin1StringView(QcWorldGenerators::empty));
        QCOMPARE(QcWorldLibraryController::generatorOf(Type::Showcase), QLatin1StringView(QcWorldGenerators::showcase));
        const QMetaEnum types = QMetaEnum::fromType<Type>();
        for (int index = 0; index < types.keyCount(); ++index) {
            const auto type = static_cast<Type>(types.value(index));
            QCOMPARE(QcWorldLibraryController::worldTypeOf(QcWorldLibraryController::generatorOf(type)), type);
        }
        QCOMPARE(QcWorldLibraryController::worldTypeOf(u"spiral"_s), Type::Terrain);
        QCOMPARE(QcWorldLibraryController::worldTypeOf(QString()), Type::Terrain);
    }

    // The world list gives each world's type and the mode of its new players as enumeration values.
    void worldListGivesTypeAndModeAsEnums()
    {
        const QcWorldLibrary library(QcWorldLibrary::defaultRoot());
        QcWorldLibrary::NewWorld flat;
        flat.generator = QString::fromLatin1(QcWorldGenerators::flat);
        flat.gameMode = QcGameMode::Creative;
        QVERIFY(library.create(u"Flat one"_s, 1, flat).has_value());
        QcWorldLibrary::NewWorld showcase;
        showcase.generator = QString::fromLatin1(QcWorldGenerators::showcase);
        showcase.gameMode = QcGameMode::Spectator;
        QVERIFY(library.create(u"Show one"_s, 2, showcase).has_value());
        QVERIFY(library.create(u"Normal one"_s, 3).has_value());

        QcWorldLibraryController controller;
        controller.refreshWorlds();
        const QHash<QString, int> types = valuesBy(controller.worlds(), u"name"_s, u"worldType"_s);
        const QHash<QString, int> modes = valuesBy(controller.worlds(), u"name"_s, u"gameMode"_s);
        using Type = QcWorldLibraryController::WorldType;
        QCOMPARE(types.value(u"Flat one"_s), static_cast<int>(Type::Flat));
        QCOMPARE(types.value(u"Show one"_s), static_cast<int>(Type::Showcase));
        QCOMPARE(types.value(u"Normal one"_s), static_cast<int>(Type::Terrain));
        QCOMPARE(modes.value(u"Flat one"_s), static_cast<int>(QcGameMode::Creative));
        QCOMPARE(modes.value(u"Show one"_s), static_cast<int>(QcGameMode::Spectator));
        QCOMPARE(modes.value(u"Normal one"_s), static_cast<int>(QcGameMode::Survival));
        QVERIFY(!controller.worlds().constFirst().toMap().contains(u"generator"_s));
    }

    // A mod's state is a ModStatus.State: loaded, switched off, unreadable, and loaded on disk but
    // refused by the last world (failed, with that world's reason).
    void modStatesAreEnums()
    {
        QTemporaryDir mods;
        QVERIFY(writeText(mods.filePath(u"good/pack.json"_s), R"({"id":"good","version":"1.0.0"})"));
        QVERIFY(writeText(mods.filePath(u"off/pack.json"_s), R"({"id":"off","version":"1.0.0"})"));
        QVERIFY(writeText(mods.filePath(u"broken/pack.json"_s), R"({"id":"broken","version":"soon"})"));
        QVERIFY(writeText(mods.filePath(u"refused/pack.json"_s), R"({"id":"refused","version":"1.0.0"})"));
        QVERIFY(QcModLoader::setModEnabled(mods.path(), u"off"_s, false).has_value());

        QcWorldLibraryController controller;
        controller.setModsDirectory(mods.path());
        QcModStatus refused;
        refused.info.id = u"refused"_s;
        refused.state = QcModStatus::State::Failed;
        refused.reason = u"bad blocks"_s;
        controller.recordLoadedMods({refused});

        const QHash<QString, int> states = valuesBy(controller.mods(), u"id"_s, u"state"_s);
        QCOMPARE(states.value(u"good"_s), static_cast<int>(QcModStatus::State::Loaded));
        QCOMPARE(states.value(u"off"_s), static_cast<int>(QcModStatus::State::Disabled));
        QCOMPARE(states.value(u"refused"_s), static_cast<int>(QcModStatus::State::Failed));
        QCOMPARE(valuesBy(controller.mods(), u"name"_s, u"state"_s).value(u"broken"_s), static_cast<int>(QcModStatus::State::Failed));
        const QHash<QString, QVariant> reasons = [&controller] {
            QHash<QString, QVariant> byId;
            const QVariantList list = controller.mods();
            for (const QVariant &mod : list) {
                byId.insert(mod.toMap().value(u"id"_s).toString(), mod.toMap().value(u"reason"_s));
            }
            return byId;
        }();
        QCOMPARE(reasons.value(u"refused"_s).toString(), u"bad blocks"_s);
    }

    // A handbook page gives each recipe's kind as a HandbookModel.RecipeKind, in the order of the
    // entry's sections and recipes.
    void recipeKindsAreEnums()
    {
        QcHandbookEntry entry;
        entry.id = u"item:t:thing"_s;
        entry.title = u"Thing"_s;
        QcHandbookEntry::Recipe crafting;
        QcHandbookEntry::Recipe machine;
        machine.kind = QcHandbookEntry::Recipe::Kind::Machine;
        entry.sections = {{u"Made"_s, {}, {}, {crafting, machine}}, {u"Text only"_s, u"Words"_s, {}, {}}, {u"Smelted"_s, {}, {}, {machine}}};
        QcHandbook::Contents contents;
        contents.entries = {entry};
        QcHandbookModel model;
        model.setHandbook(std::make_shared<const QcHandbook>(contents));

        const QVariantList sections = model.page(entry.id).value(u"sections"_s).toList();
        QCOMPARE(sections.size(), 3);
        const auto kindsOf = [&sections](int section) {
            QList<int> kinds;
            const QVariantList recipes = sections[section].toMap().value(u"recipes"_s).toList();
            for (const QVariant &recipe : recipes) {
                kinds.append(recipe.toMap().value(u"kind"_s).toInt());
            }
            return kinds;
        };
        using Kind = QcHandbookModel::RecipeKind;
        QCOMPARE(kindsOf(0), (QList<int>{static_cast<int>(Kind::Crafting), static_cast<int>(Kind::Machine)}));
        QCOMPARE(kindsOf(1), QList<int>{});
        QCOMPARE(kindsOf(2), QList<int>{static_cast<int>(Kind::Machine)});
        QCOMPARE(sections[1].toMap().value(u"text"_s).toString(), u"Words"_s);
        QVERIFY(model.page(u"item:t:none"_s).isEmpty());
    }

    // The technical facts of a page come apart from the others, each in their order.
    void technicalFactsComeApart()
    {
        QcHandbookEntry entry;
        entry.id = u"item:t:thing"_s;
        entry.title = u"Thing"_s;
        entry.facts = {{u"Stack"_s, u"64"_s, {}, false}, {u"Id"_s, u"t:thing"_s, {}, true},
                       {u"Tool"_s, u"Axe"_s, {}, false}, {u"Sound"_s, u"wood"_s, {}, true}};
        QcHandbook::Contents contents;
        contents.entries = {entry};
        QcHandbookModel model;
        model.setHandbook(std::make_shared<const QcHandbook>(contents));

        const QVariantMap page = model.page(entry.id);
        const auto labels = [&page](const QString &key) {
            QStringList found;
            const QVariantList facts = page.value(key).toList();
            for (const QVariant &fact : facts) {
                found.append(fact.toMap().value(u"label"_s).toString());
            }
            return found;
        };
        QCOMPARE(labels(u"facts"_s), (QStringList{u"Stack"_s, u"Tool"_s}));
        QCOMPARE(labels(u"technicalFacts"_s), (QStringList{u"Id"_s, u"Sound"_s}));
    }

    // The on-screen keyboard's layout follows the UI language shown now: QWERTZ in German.
    void keyboardLayoutFollowsTheLanguage()
    {
        using Layout = QcSettingsController::KeyboardLayout;
        QCOMPARE(QcSettingsController::keyboardLayoutOf(u"de"_s), Layout::Qwertz);
        QCOMPARE(QcSettingsController::keyboardLayoutOf(u"en"_s), Layout::Qwerty);
        QCOMPARE(QcSettingsController::keyboardLayoutOf(QString()), Layout::Qwerty);

        QcSessionParts parts;
        QcAudioEngine audio;
        QcWorldLibraryController library;
        QcSettingsController settings(parts, library, &audio);
        QCOMPARE(settings.property("keyboardLayout").value<Layout>(), Layout::Qwerty);
        settings.setContentLanguage(u"de"_s);
        QCOMPARE(settings.property("keyboardLayout").value<Layout>(), Layout::Qwertz);
    }

    // A new world's type and mode come as enumeration values; any other value (a name, a number
    // out of range) is refused with a warning, and no world is created.
    void createWorldRefusesUnknownTypesAndModes_data()
    {
        QTest::addColumn<QVariantMap>("setup");
        QTest::addColumn<QString>("warning");
        QTest::newRow("type as a name") << QVariantMap{{u"worldType"_s, u"flat"_s}} << u"world type"_s;
        QTest::newRow("type out of range") << QVariantMap{{u"worldType"_s, 4}} << u"world type"_s;
        QTest::newRow("type a fraction") << QVariantMap{{u"worldType"_s, 1.5}} << u"world type"_s;
        QTest::newRow("mode as a name") << QVariantMap{{u"gameMode"_s, u"creative"_s}} << u"game mode"_s;
        QTest::newRow("mode out of range") << QVariantMap{{u"gameMode"_s, QcGameModes::count}} << u"game mode"_s;
        QTest::newRow("mode negative") << QVariantMap{{u"gameMode"_s, -1}} << u"game mode"_s;
    }

    void createWorldRefusesUnknownTypesAndModes()
    {
        QFETCH(QVariantMap, setup);
        QFETCH(QString, warning);
        QcGameController game;
        const qsizetype before = QcWorldLibrary(QcWorldLibrary::defaultRoot()).list().size();

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"createWorld: refused the "_s + warning));
        game.createWorld(u"Refused"_s, u"1"_s, setup);
        QCOMPARE(game.phase(), QcGameController::Phase::Error);
        QCOMPARE(QcWorldLibrary(QcWorldLibrary::defaultRoot()).list().size(), before);
    }

    // The menu's enum values (also as whole doubles, as QML may hand them over) make a world of
    // that type and mode; world.json keeps the generator's name and the mode.
    void createWorldTakesTheEnumValues_data()
    {
        QTest::addColumn<QVariant>("type");
        QTest::addColumn<QVariant>("mode");
        QTest::addColumn<QString>("generator");
        QTest::addColumn<QcGameMode>("expectedMode");
        using Type = QcWorldLibraryController::WorldType;
        QTest::newRow("flat creative") << QVariant(static_cast<int>(Type::Flat)) << QVariant(static_cast<int>(QcGameMode::Creative))
                                       << QString::fromLatin1(QcWorldGenerators::flat) << QcGameMode::Creative;
        QTest::newRow("void adventure, doubles") << QVariant(static_cast<double>(Type::Void)) << QVariant(static_cast<double>(QcGameMode::Adventure))
                                                 << QString::fromLatin1(QcWorldGenerators::empty) << QcGameMode::Adventure;
    }

    void createWorldTakesTheEnumValues()
    {
        QFETCH(QVariant, type);
        QFETCH(QVariant, mode);
        QFETCH(QString, generator);
        QFETCH(QcGameMode, expectedMode);
        const QString name = u"Made "_s + QString::fromLatin1(QTest::currentDataTag());
        {
            QcGameController game;
            game.createWorld(name, u"1"_s, {{u"worldType"_s, type}, {u"gameMode"_s, mode}});
            QVERIFY(game.phase() != QcGameController::Phase::Error);
        }
        const QList<QcWorldLibrary::Entry> worlds = QcWorldLibrary(QcWorldLibrary::defaultRoot()).list();
        const auto made = std::ranges::find_if(worlds, [&name](const QcWorldLibrary::Entry &entry) { return entry.info.name == name; });
        QVERIFY(made != worlds.end());
        QCOMPARE(made->info.generator, generator);
        QCOMPARE(made->info.defaultGameMode, expectedMode);
    }

    // The test hooks QTCRAFT_MENU_WORLD_TYPE and QTCRAFT_MENU_GAME_MODE choose in the new world's
    // draft, whose choices the real page's dropdowns show; unknown names are refused with a warning.
    void testHooksPreselectTheNewWorld()
    {
        QcGameController game;
        QcNewWorldDraft &draft = *game.worldOptions()->draft();
        QQmlEngine engine;
        const std::unique_ptr<QObject> page = newWorldPage(engine, game);
        QVERIFY(page);
        QObject *types = page->findChild<QObject *>(u"worldTypeChoice"_s);
        QObject *modes = page->findChild<QObject *>(u"gameModeChoice"_s);
        QVERIFY(types && modes);
        QCOMPARE(types->property("currentValue").toInt(), static_cast<int>(QcWorldLibraryController::WorldType::Terrain));

        QcMenuPreselect::newWorld(draft, u"showcase"_s, u"creative"_s);
        QCOMPARE(draft.worldType(), QcWorldLibraryController::WorldType::Showcase);
        QCOMPARE(draft.gameMode(), QcGameMode::Creative);
        QCOMPARE(types->property("currentValue").toInt(), static_cast<int>(QcWorldLibraryController::WorldType::Showcase));
        QCOMPARE(modes->property("currentValue").toInt(), static_cast<int>(QcGameMode::Creative));
        QVERIFY(!page->property("terrain").toBool());

        // Opening the form again starts with the hooks' choices.
        draft.setWorldType(QcWorldLibraryController::WorldType::Flat);
        QCOMPARE(types->property("currentValue").toInt(), static_cast<int>(QcWorldLibraryController::WorldType::Flat));
        QVERIFY(QMetaObject::invokeMethod(page.get(), "open"));
        QCOMPARE(draft.worldType(), QcWorldLibraryController::WorldType::Showcase);
        QCOMPARE(types->property("currentValue").toInt(), static_cast<int>(QcWorldLibraryController::WorldType::Showcase));

        // The seed field writes into the draft and shows what a reset leaves.
        QObject *seed = page->findChild<QObject *>(u"worldSeed"_s);
        QVERIFY(seed);
        seed->setProperty("text", u" 1234 "_s);
        QCOMPARE(draft.seedText(), u" 1234 "_s);
        QCOMPARE(draft.seed(), u"1234"_s);
        QCOMPARE(seed->property("placeholderText").toString(), draft.randomSeed());
        draft.reset();
        QCOMPARE(seed->property("text").toString(), QString());

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"unknown world type"_s));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"unknown game mode"_s));
        QcMenuPreselect::newWorld(draft, u"spiral"_s, u"hardcore"_s);
        QCOMPARE(draft.worldType(), QcWorldLibraryController::WorldType::Showcase);
    }

    // The page's "Generator version" (decision E-922) lists the versions newest first, the
    // current one as "Current (N)", and shows and sets the draft's choice.
    void theNewWorldPageChoosesTheGeneratorVersion()
    {
        QcGameController game;
        QcNewWorldDraft &draft = *game.worldOptions()->draft();
        QQmlEngine engine;
        const std::unique_ptr<QObject> page = newWorldPage(engine, game);
        QVERIFY(page);
        QObject *versions = page->findChild<QObject *>(u"generatorVersionChoice"_s);
        QVERIFY(versions);
        QCOMPARE(versions->property("count").toInt(), QcGeneratorVersion::current - QcGeneratorVersion::oldest + 1);
        QCOMPARE(versions->property("currentValue").toInt(), QcGeneratorVersion::current);
        QCOMPARE(versions->property("displayText").toString(), u"Current (%1)"_s.arg(QcGeneratorVersion::current));
        QVERIFY(versions->property("popup").value<QObject *>());

        draft.setGeneratorVersion(QcGeneratorVersion::oldest);
        QCOMPARE(versions->property("currentValue").toInt(), QcGeneratorVersion::oldest);
        QCOMPARE(versions->property("displayText").toString(), u"Version %1"_s.arg(QcGeneratorVersion::oldest));
        QVERIFY(QMetaObject::invokeMethod(page.get(), "open"));
        QCOMPARE(draft.generatorVersion(), QcGeneratorVersion::current);
        QCOMPARE(versions->property("currentValue").toInt(), QcGeneratorVersion::current);

        // A choice in the list sets the draft's version.
        QVERIFY(QMetaObject::invokeMethod(versions, "activated", Q_ARG(int, 1)));
        QCOMPARE(draft.generatorVersion(), QcGeneratorVersion::current - 1);
    }

    // "Continue" opens the world played last that can be opened; the list below shows the others,
    // also a newer one that cannot be opened. QML sees null when there is none.
    void theWorldListPicksTheWorldToContinue()
    {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        const bool wasSet = qEnvironmentVariableIsSet("QTCRAFT_DATA_DIR");
        const QByteArray before = qgetenv("QTCRAFT_DATA_DIR");
        const auto restore = qScopeGuard([wasSet, before] {
            if (wasSet) {
                qputenv("QTCRAFT_DATA_DIR", before);
            } else {
                qunsetenv("QTCRAFT_DATA_DIR");
            }
        });
        qputenv("QTCRAFT_DATA_DIR", data.path().toUtf8());
        QcWorldLibraryController controller;
        controller.refreshWorlds();
        QVERIFY(controller.continueWorld().isNull());
        QVERIFY(controller.otherWorlds().isEmpty());

        const QcWorldLibrary library(QcWorldLibrary::defaultRoot());
        // Created at the same moment, "Newer" still comes first (by name).
        QVERIFY(library.create(u"Older"_s, 1).has_value());
        const QcResult<QcWorldLibrary::Entry> newer = library.create(u"Newer"_s, 2);
        QVERIFY(newer.has_value());
        controller.refreshWorlds();
        QCOMPARE(controller.continueWorld().toMap().value(u"name"_s).toString(), u"Newer"_s);
        QCOMPARE(controller.otherWorlds().size(), 1);
        QCOMPARE(controller.otherWorlds().constFirst().toMap().value(u"name"_s).toString(), u"Older"_s);

        controller.noteUnplayable(newer->folder, u"damaged"_s);
        QCOMPARE(controller.continueWorld().toMap().value(u"name"_s).toString(), u"Older"_s);
        QCOMPARE(controller.otherWorlds().size(), 1);
        QCOMPARE(controller.otherWorlds().constFirst().toMap().value(u"name"_s).toString(), u"Newer"_s);

        controller.noteUnplayable(controller.continueWorld().toMap().value(u"folder"_s).toString(), u"damaged too"_s);
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQml\nQtObject { required property var library\n"
                          "readonly property bool none: library.continueWorld === null\n"
                          "readonly property int others: library.otherWorlds.length }\n",
                          QUrl(u"qrc:/test_continue_world.qml"_s));
        const std::unique_ptr<QObject> object(component.createWithInitialProperties({{u"library"_s, QVariant::fromValue(&controller)}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        QVERIFY(object->property("none").toBool());
        QCOMPARE(object->property("others").toInt(), 2);
    }

    // QML knows the enumerations under their names, with the values of C++.
    void qmlKnowsTheEnums()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQml
import QtCraft

QtObject {
    readonly property int creative: GameMode.Mode.Creative
    readonly property int spectator: GameMode.Mode.Spectator
    readonly property int flat: WorldLibrary.WorldType.Flat
    readonly property int showcase: WorldLibrary.WorldType.Showcase
    readonly property int failed: ModStatus.State.Failed
    readonly property int fromMod: TexturePack.Origin.Mod
    readonly property int machine: HandbookModel.RecipeKind.Machine
    readonly property int qwertz: GameSettings.KeyboardLayout.Qwertz
}
)",
                          QUrl(u"qrc:/test_menu_enums.qml"_s));
        const std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        QCOMPARE(object->property("creative").toInt(), static_cast<int>(QcGameMode::Creative));
        QCOMPARE(object->property("spectator").toInt(), static_cast<int>(QcGameMode::Spectator));
        QCOMPARE(object->property("flat").toInt(), static_cast<int>(QcWorldLibraryController::WorldType::Flat));
        QCOMPARE(object->property("showcase").toInt(), static_cast<int>(QcWorldLibraryController::WorldType::Showcase));
        QCOMPARE(object->property("failed").toInt(), static_cast<int>(QcModStatus::State::Failed));
        QCOMPARE(object->property("fromMod").toInt(), static_cast<int>(QcTexturePack::Origin::Mod));
        QCOMPARE(object->property("machine").toInt(), static_cast<int>(QcHandbookModel::RecipeKind::Machine));
        QCOMPARE(object->property("qwertz").toInt(), static_cast<int>(QcSettingsController::KeyboardLayout::Qwertz));
    }

private:
    // The real "New world" page for `game` (null with a warning when it does not load).
    static std::unique_ptr<QObject> newWorldPage(QQmlEngine &engine, QcGameController &game)
    {
        QQmlComponent component(&engine);
        component.setData("import QtCraft\nNewWorldPage { objectName: \"newWorldPage\" }\n", QUrl(u"qrc:/test_new_world.qml"_s));
        std::unique_ptr<QObject> page(component.createWithInitialProperties({{u"game"_s, QVariant::fromValue(&game)}}));
        if (!page) {
            qWarning().noquote() << component.errorString();
        }
        return page;
    }

    QTemporaryDir m_data;
};

QTEST_MAIN(TestMenuEnums)
#include "test_menu_enums.moc"
