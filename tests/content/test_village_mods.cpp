// Village data that mods extend (decision E-421): professions in their own files, villager species
// named by the creature kind, styles extended in pack order, residents, formatVersion and the
// files of the earlier format refused with where their content went.

#include "common/content/loader/QcBaseContent.h"
#include "common/content/loader/QcContentPackLoader.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/content/registry/QcVillagerOutfits.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <utility>

using namespace Qt::StringLiterals;

namespace {

void writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
}

// A pack `id` in `root` with `files` (path relative to the pack -> content).
QString writePack(const QString &root, const QString &id, const QList<std::pair<QString, QByteArray>> &files)
{
    const QString directory = QDir(root).filePath(id);
    writeFile(QDir(directory).filePath(u"pack.json"_s), u"{\"id\": \"%1\", \"version\": \"1.0.0\"}"_s.arg(id).toUtf8());
    for (const auto &[path, content] : files) {
        writeFile(QDir(directory).filePath(path), content);
    }
    return directory;
}

struct Loaded
{
    std::shared_ptr<QcContentRegistry> content;
    QStringList warnings;
};

// The base pack and then `packs` in that order, finished.
Loaded load(const QStringList &packs)
{
    Loaded loaded{std::make_shared<QcContentRegistry>(), {}};
    const QcResult<QcLoadedPack> base = QcBaseContent::loadInto(*loaded.content);
    if (!base) {
        return loaded;
    }
    loaded.warnings = base->warnings;
    for (const QString &pack : packs) {
        const QcResult<QcLoadedPack> read = QcContentPackLoader::load(pack, *loaded.content);
        loaded.warnings += read ? read->warnings : QStringList{read.error()};
    }
    loaded.warnings += loaded.content->finish();
    return loaded;
}

bool anyContains(const QStringList &warnings, const QString &text)
{
    return std::ranges::any_of(warnings, [&text](const QString &warning) { return warning.contains(text); });
}

const QByteArray gnomeMob = R"({"name": "gnome", "villager": "gnome", "health": 10, "width": 0.5, "height": 1.2,
    "walkSpeed": 1.2, "chaseSpeed": 2.0, "hostile": false, "spawn": {"weight": 0}, "skeleton": "humanoid", "outfit": "qtcraft:villager_tunic"})";

} // namespace

class TestVillageMods : public QObject
{
    Q_OBJECT

private slots:
    void twoModsAddingProfessionsCoexist()
    {
        QTemporaryDir dir;
        const QString baker = writePack(dir.path(), u"bakery"_s,
                                        {{u"professions/baker.json"_s, R"({"formatVersion": 1, "name": "baker", "workstations": ["qtcraft:crafting_table"],
                                            "layers": {"torso": {"texture": "qtcraft:villager_farmer_apron", "tint": "none"}}})"},
                                         {u"trades/baker.json"_s, R"({"name": "baker", "profession": "baker", "offers": [
                                            {"level": 1, "costs": [{"item": "qtcraft:wheat", "count": 20}], "result": {"item": "qtcraft:emerald"}}]})"}});
        const QString librarian = writePack(dir.path(), u"library"_s,
                                            {{u"professions/librarian.json"_s,
                                              R"({"name": "librarian", "workstations": ["qtcraft:bookshelf"], "offersPerLevel": 3,
                                                  "routine": [{"from": 0.0, "do": "work"}, {"from": 0.6, "do": "sleep"}]})"}});
        const Loaded loaded = load({baker, librarian});
        QVERIFY2(loaded.warnings.isEmpty(), qPrintable(loaded.warnings.join(u'\n')));

        // Both professions and the base ones, each species with all of them.
        const QcContentRegistry &content = *loaded.content;
        QCOMPARE(content.villageCatalog().professions().size(), 6);
        QVERIFY(content.villageCatalog().profession(u"bakery:baker"_s));
        QVERIFY(content.villageCatalog().profession(u"library:librarian"_s));
        const QcVillagerSettings &species = content.villagerSettingsOf(*content.mobIndex(u"qtcraft:villager"_s));
        QCOMPARE(species.professions.size(), 6);
        QCOMPARE(species.forWorkstation(u"bakery:baker"_s)->name, u"bakery:baker"_s);
        const QHash<QString, QString> &workstations = content.villageCatalog().settings().workstations;
        QCOMPARE(workstations.value(u"qtcraft:crafting_table"_s), u"bakery:baker"_s);
        QCOMPARE(workstations.value(u"qtcraft:bookshelf"_s), u"library:librarian"_s);
        QCOMPARE(workstations.value(u"qtcraft:composter"_s), u"qtcraft:farmer"_s);

        // The baker's apron is joined with the villagers' outfit; the librarian has its own routine
        // and number of offers.
        const QcVillagerProfession &bakerProfession = *content.villageCatalog().profession(u"bakery:baker"_s);
        QVERIFY(content.assets().appearance(QcVillagerOutfits::outfitName(u"qtcraft:villager_tunic"_s, bakerProfession)));
        QCOMPARE(content.villageCatalog().profession(u"library:librarian"_s)->offersPerLevel, 3);
        QCOMPARE(species.activityAt(0.7, u"library:librarian"_s), QcVillagerActivity::Sleep);
        QCOMPARE(species.activityAt(0.25, u"library:librarian"_s), QcVillagerActivity::Work);
        QCOMPARE(species.activityAt(0.7, u"qtcraft:farmer"_s), QcVillagerActivity::Sleep);
        QCOMPARE(species.activityAt(0.5, u"library:librarian"_s), QcVillagerActivity::Work);
        QCOMPARE(species.activityAt(0.5, u"qtcraft:farmer"_s), QcVillagerActivity::Home);
        QVERIFY(std::ranges::any_of(content.villageCatalog().tradeSets(), [](const QcTradeSet &set) { return set.profession == u"bakery:baker"_s; }));
    }

    void aBlockTwoProfessionsNameGoesToTheLaterOne()
    {
        QTemporaryDir dir;
        const QString compost = writePack(dir.path(), u"compost"_s,
                                          {{u"professions/gardener.json"_s, R"({"name": "gardener", "workstations": ["qtcraft:composter"]})"}});
        const Loaded loaded = load({compost});
        QVERIFY(anyContains(loaded.warnings, u"'compost:gardener' keeps it"_s));
        QCOMPARE(loaded.content->villageCatalog().settings().workstations.value(u"qtcraft:composter"_s), u"compost:gardener"_s);
    }

    void modsExtendThePlainsStyleInPackOrder()
    {
        QTemporaryDir dir;
        const qsizetype pieces = load({}).content->villageCatalog().style(u"qtcraft:plains"_s)->pieces.size();
        const QString forest = writePack(dir.path(), u"forest_fields"_s,
                                         {{u"villages/plains.json"_s, R"({"formatVersion": 1, "extends": "qtcraft:plains",
                                             "add": {"biomes": ["qtcraft:forest", "qtcraft:plains"], "crops": [{"block": "qtcraft:pumpkin_stem", "weight": 2}]}})"},
                                          {u"villages/meadow.json"_s, R"({"name": "meadow", "extends": "qtcraft:plains", "biomes": ["qtcraft:mountains"],
                                             "fallback": false, "add": {"pieces": [{"structure": "qtcraft:village_plains_tower", "max": 1}]}})"}});
        const QString birch = writePack(dir.path(), u"birch_fields"_s,
                                        {{u"villages/more_plains.json"_s, R"({"extends": "qtcraft:plains", "add": {"biomes": ["qtcraft:birch_forest"],
                                            "layouts": [{"kind": "plaza", "weight": 4}]}})"}});
        const Loaded loaded = load({forest, birch});
        QVERIFY2(loaded.warnings.isEmpty(), qPrintable(loaded.warnings.join(u'\n')));

        // Both mods' biomes, once each, the crop and the layout; the rest as the base has it.
        const QcVillageStyleDefinition *plains = loaded.content->villageCatalog().style(u"qtcraft:plains"_s);
        QVERIFY(plains);
        QCOMPARE(plains->biomes, (QStringList{u"qtcraft:plains"_s, u"qtcraft:forest_steppe"_s, u"qtcraft:forest"_s, u"qtcraft:birch_forest"_s}));
        QCOMPARE(plains->crops.constLast().block, u"qtcraft:pumpkin_stem"_s);
        QCOMPARE(plains->crops.size(), 4);
        QCOMPARE(plains->layouts.size(), 7);
        QCOMPARE(plains->pieces.size(), pieces);
        QVERIFY(plains->fallback);
        QCOMPARE(plains->centre, u"qtcraft:village_plains_well"_s);

        // A named extension is a new style from a copy of the base as it was then.
        const QcVillageStyleDefinition *meadow = loaded.content->villageCatalog().style(u"forest_fields:meadow"_s);
        QVERIFY(meadow);
        QCOMPARE(meadow->biomes, QStringList{u"qtcraft:mountains"_s});
        QVERIFY(!meadow->fallback);
        QCOMPARE(meadow->pieces.size(), pieces + 1);
        QCOMPARE(meadow->look.outfit, plains->look.outfit);
    }

    void anExtensionOfAnUnknownStyleIsLeftOut()
    {
        QTemporaryDir dir;
        const QString pack = writePack(dir.path(), u"lost"_s,
                                       {{u"villages/moon.json"_s, R"({"extends": "moon", "add": {"biomes": ["qtcraft:forest"]}})"},
                                        {u"villages/broken.json"_s, R"({"name": "broken", "centre": "qtcraft:village_plains_well",
                                            "pieces": [{"structure": "qtcraft:village_plains_tower"}], "add": {"biomes": ["qtcraft:forest"]}})"}});
        const Loaded loaded = load({pack});
        QVERIFY(anyContains(loaded.warnings, u"names no village style: 'moon'"_s));
        QVERIFY(anyContains(loaded.warnings, u"'add' and 'remove' need 'extends'"_s));
        QVERIFY(!loaded.content->villageCatalog().style(u"lost:broken"_s));
    }

    void aSecondVillagerSpeciesLivesByItsOwnSettings()
    {
        QTemporaryDir dir;
        const QString pack = writePack(dir.path(), u"gnomes"_s,
                                       {{u"mobs/gnome.json"_s, gnomeMob},
                                        {u"villagers/gnome.json"_s, R"({"formatVersion": 1, "name": "gnome", "unemployed": "qtcraft:unemployed",
                                            "routine": [{"from": 0.0, "do": "sleep"}, {"from": 0.5, "do": "work"}], "fleeDistance": 4,
                                            "trading": {"offersPerLevel": 1}, "look": {"outfit": "qtcraft:villager_coat"}})"},
                                        {u"villages/gnome_village.json"_s, R"({"name": "gnome_village", "extends": "qtcraft:plains", "biomes": [],
                                            "fallback": false, "villager": "gnome", "golem": false,
                                            "residents": [{"mob": "qtcraft:pig", "min": 1, "max": 3}, {"mob": "qtcraft:sheep"}]})"}});
        const Loaded loaded = load({pack});
        QVERIFY2(loaded.warnings.isEmpty(), qPrintable(loaded.warnings.join(u'\n')));

        const QcContentRegistry &content = *loaded.content;
        const int gnome = *content.mobIndex(u"gnomes:gnome"_s);
        const int villager = *content.mobIndex(u"qtcraft:villager"_s);
        QVERIFY(content.mobs()[gnome].villager);
        const QcVillagerSettings &gnomes = content.villagerSettingsOf(gnome);
        QCOMPARE(gnomes.name, u"gnomes:gnome"_s);
        QCOMPARE(gnomes.fleeDistance, 4.0);
        QCOMPARE(gnomes.trading.offersPerLevel, 1);
        QCOMPARE(gnomes.activityAt(0.25, u"qtcraft:farmer"_s), QcVillagerActivity::Sleep);
        QCOMPARE(content.villagerSettingsOf(villager).activityAt(0.25, u"qtcraft:farmer"_s), QcVillagerActivity::Work);
        QCOMPARE(gnomes.professions.size(), 4);

        // The style's villages are the gnomes', without golems; other villages keep the base's.
        QCOMPARE(content.villagerOfStyle(u"gnomes:gnome_village"_s), gnome);
        QVERIFY(content.villageCatalog().golemOfStyle(u"gnomes:gnome_village"_s).mob.isEmpty());
        QCOMPARE(content.villagerOfStyle(u"qtcraft:plains"_s), villager);
        QCOMPARE(content.villageCatalog().golemOfStyle(u"qtcraft:plains"_s).mob, u"qtcraft:iron_golem"_s);

        // Residents are data: kind and count per village.
        const QList<QcVillageResident> &residents = content.villageCatalog().style(u"gnomes:gnome_village"_s)->residents;
        QCOMPARE(residents.size(), 2);
        QCOMPARE(residents[0].mob, u"qtcraft:pig"_s);
        QCOMPARE(residents[0].max, 3);
        QCOMPARE(residents[1].min, 1);
        QCOMPARE(residents[1].max, 1);
        QVERIFY(content.villageCatalog().style(u"qtcraft:plains"_s)->residents.isEmpty());
    }

    void aNamedCopyStartsWithoutTheBaseBiomes()
    {
        QTemporaryDir dir;
        const QString pack = writePack(dir.path(), u"copies"_s,
                                       {{u"villages/copy.json"_s, R"({"name": "copy", "extends": "qtcraft:plains", "add": {"crops": ["qtcraft:carrot_crop"]}})"},
                                        {u"villages/desert.json"_s, R"({"extends": "qtcraft:desert", "remove": {"biomes": ["qtcraft:desert"]}})"},
                                        {u"villages/dunes.json"_s, R"({"name": "dunes", "extends": "qtcraft:desert", "biomes": ["qtcraft:desert"]})"}});
        const Loaded loaded = load({pack});
        QVERIFY2(loaded.warnings.isEmpty(), qPrintable(loaded.warnings.join(u'\n')));

        // The copy builds nowhere until it names biomes; the plains stay the fallback.
        const QcContentRegistry &content = *loaded.content;
        QVERIFY(content.villageCatalog().style(u"copies:copy"_s)->biomes.isEmpty());
        QVERIFY(!content.villageCatalog().style(u"copies:copy"_s)->fallback);
        QVERIFY(content.villageCatalog().style(u"qtcraft:plains"_s)->fallback);
        QCOMPARE(content.villageCatalog().style(u"qtcraft:plains"_s)->biomes, (QStringList{u"qtcraft:plains"_s, u"qtcraft:forest_steppe"_s}));

        // `remove` moves the desert from its base style to the mod's.
        QVERIFY(content.villageCatalog().style(u"qtcraft:desert"_s)->biomes.isEmpty());
        QCOMPARE(content.villageCatalog().style(u"copies:dunes"_s)->biomes, QStringList{u"qtcraft:desert"_s});
    }

    void aBiomeTwoStylesNameGoesToTheLaterChangedOne()
    {
        QTemporaryDir dir;
        const QString first = writePack(dir.path(), u"first"_s,
                                        {{u"villages/meadow.json"_s, R"({"name": "meadow", "extends": "qtcraft:plains", "biomes": ["qtcraft:plains"],
                                            "fallback": true})"}});
        const QString second = writePack(dir.path(), u"second"_s,
                                         {{u"villages/taiga.json"_s, R"({"extends": "qtcraft:taiga", "add": {"biomes": ["qtcraft:plains", "qtcraft:moon"]}})"}});
        const Loaded loaded = load({first, second});
        const QcContentRegistry &content = *loaded.content;
        QVERIFY(anyContains(loaded.warnings, u"'first:meadow' and 'qtcraft:taiga' both name biome 'qtcraft:plains'; 'qtcraft:taiga', changed later"_s));
        QVERIFY(anyContains(loaded.warnings, u"'qtcraft:plains' and 'qtcraft:taiga' both name biome 'qtcraft:plains'"_s));
        QVERIFY(anyContains(loaded.warnings, u"'qtcraft:plains' and 'first:meadow' are both the fallback; 'first:meadow'"_s));
        QVERIFY(anyContains(loaded.warnings, u"village style 'qtcraft:taiga': unknown biome 'qtcraft:moon'"_s));
        QCOMPARE(content.villageCatalog().style(u"qtcraft:taiga"_s)->biomes, (QStringList{u"qtcraft:taiga"_s, u"qtcraft:plains"_s, u"qtcraft:moon"_s}));
        // The plains style keeps only its other biome.
        QCOMPARE(content.villageCatalog().style(u"qtcraft:plains"_s)->biomes, QStringList{u"qtcraft:forest_steppe"_s});
        QVERIFY(content.villageCatalog().style(u"first:meadow"_s)->biomes.isEmpty());
        QVERIFY(content.villageCatalog().style(u"first:meadow"_s)->fallback);
        QVERIFY(!content.villageCatalog().style(u"qtcraft:plains"_s)->fallback);
    }

    void extensionsBuildOnExtensionsAndAFullDefinitionDropsThem()
    {
        QTemporaryDir dir;
        const QString first = writePack(dir.path(), u"first"_s,
                                        {{u"villages/a_meadow.json"_s, R"({"name": "meadow", "extends": "qtcraft:plains", "biomes": ["qtcraft:mountains"]})"},
                                         {u"villages/b_meadow_crops.json"_s, R"({"extends": "meadow", "add": {"crops": ["qtcraft:carrot_crop"]}})"},
                                         {u"villages/plains.json"_s, R"({"extends": "qtcraft:plains", "add": {"crops": ["qtcraft:carrot_crop"]}})"}});
        const QString second = writePack(dir.path(), u"second"_s,
                                         {{u"villages/meadow.json"_s, R"({"extends": "first:meadow", "add": {"crops": ["qtcraft:potato_crop"]}})"},
                                          {u"villages/plains.json"_s, R"({"name": "qtcraft:plains", "biomes": ["qtcraft:plains"], "fallback": true,
                                             "centre": "qtcraft:village_plains_well", "pieces": [{"structure": "qtcraft:village_plains_tower"}]})"}});
        const Loaded loaded = load({first, second});
        QVERIFY2(loaded.warnings.isEmpty(), qPrintable(loaded.warnings.join(u'\n')));

        const QcVillageStyleDefinition *meadow = loaded.content->villageCatalog().style(u"first:meadow"_s);
        const qsizetype baseCrops = load({}).content->villageCatalog().style(u"qtcraft:plains"_s)->crops.size();
        QCOMPARE(meadow->crops.size(), baseCrops + 2);
        QCOMPARE(meadow->crops[baseCrops].block, u"qtcraft:carrot_crop"_s);
        QCOMPARE(meadow->crops.constLast().block, u"qtcraft:potato_crop"_s);
        const QcVillageStyleDefinition *plains = loaded.content->villageCatalog().style(u"qtcraft:plains"_s);
        QVERIFY(plains->crops.isEmpty());
        QCOMPARE(plains->pieces.size(), 1);
    }

    void anExtensionBeforeTheFileDefiningItsBaseSaysSo()
    {
        QTemporaryDir dir;
        const QString pack = writePack(dir.path(), u"order"_s,
                                       {{u"villages/a_more.json"_s, R"({"extends": "meadow", "add": {"crops": ["qtcraft:carrot_crop"]}})"},
                                        {u"villages/b_meadow.json"_s, R"({"name": "meadow", "extends": "qtcraft:plains"})"},
                                        {u"villages/c_bad.json"_s, R"({"extends": 3})"},
                                        {u"villages/d_bad_name.json"_s, R"({"name": "Bad Name!", "extends": "qtcraft:plains"})"}});
        const Loaded loaded = load({pack});
        QVERIFY(anyContains(loaded.warnings, u"'extends' names 'order:meadow', which the later file"_s));
        QVERIFY(anyContains(loaded.warnings, u"b_meadow.json defines; rename this file to sort after it"_s));
        QVERIFY(anyContains(loaded.warnings, u"c_bad.json: village style: 'extends' must name a village style"_s));
        QVERIFY(anyContains(loaded.warnings, u"d_bad_name.json: village style extending 'qtcraft:plains'"_s));
        QVERIFY(loaded.content->villageCatalog().style(u"order:meadow"_s)->crops.size() == loaded.content->villageCatalog().style(u"qtcraft:plains"_s)->crops.size());
    }

    void villageSettingsOfALaterPackChangeOnlyTheirFields()
    {
        QTemporaryDir dir;
        const QString first = writePack(dir.path(), u"first"_s,
                                        {{u"worldgen/villages.json"_s, R"({"spacing": 512, "golem": {"cooldownSeconds": 60}})"},
                                         {u"villages/guarded.json"_s, R"({"extends": "qtcraft:plains", "name": "guarded", "golem": {"minVillagers": 5, "grudgeSeconds": 90}})"}});
        const QString second = writePack(dir.path(), u"second"_s,
                                         {{u"mobs/golem.json"_s, QByteArray(gnomeMob).replace(R"("villager": "gnome", )", "")},
                                          {u"worldgen/villages.json"_s, R"({"golem": {"mob": "gnome"}})"}});
        const Loaded loaded = load({first, second});
        QVERIFY2(loaded.warnings.isEmpty(), qPrintable(loaded.warnings.join(u'\n')));

        // Villagers and golems stay; each pack changes what it gives.
        const QcContentRegistry &content = *loaded.content;
        const QcVillageSettings &settings = content.villageCatalog().settings();
        QCOMPARE(settings.spacing, 512);
        QCOMPARE(settings.villager, u"qtcraft:villager"_s);
        QCOMPARE(settings.bell, u"qtcraft:bell"_s);
        QCOMPARE(settings.golem.mob, u"second:gnome"_s);
        QCOMPARE(settings.golem.cooldownSeconds, 60.0);
        QCOMPARE(settings.golem.gossipChance, 0.1);

        // A style's golem fields go over the settings' as they end up.
        const QcVillageGolemSettings guarded = content.villageCatalog().golemOfStyle(u"first:guarded"_s);
        QCOMPARE(guarded.mob, u"second:gnome"_s);
        QCOMPARE(guarded.minVillagers, 5);
        QCOMPARE(guarded.cooldownSeconds, 60.0);
        QCOMPARE(guarded.grudgeSeconds, 90.0);
        QCOMPARE(guarded.gatherDistance, 10.0);
        QCOMPARE(content.villageCatalog().golemOfStyle(u"qtcraft:plains"_s).minVillagers, 3);
    }

    void namesTheContentLacksAreWarnedAbout()
    {
        QTemporaryDir dir;
        const QString pack = writePack(dir.path(), u"odd"_s,
                                       {{u"mobs/gnome.json"_s, gnomeMob},
                                        {u"villagers/gnome.json"_s, R"({"name": "gnome", "unemployed": "qtcraft:farmer",
                                            "routine": [{"from": 0.0, "do": "work"}], "look": {"outfit": "qtcraft:villager_coat"}})"},
                                        {u"villages/odd.json"_s, R"({"name": "odd", "extends": "qtcraft:plains", "villager": "qtcraft:pig",
                                            "residents": [{"mob": "unicorn"}]})"}});
        const Loaded loaded = load({pack});
        QVERIFY(anyContains(loaded.warnings, u"village style 'odd:odd': 'villager' names no villager creature: 'qtcraft:pig'"_s));
        QVERIFY(anyContains(loaded.warnings, u"village style 'odd:odd': unknown resident 'odd:unicorn'"_s));

        // The species with a working "unemployed" is left out, and its creatures are no villagers.
        QVERIFY(anyContains(loaded.warnings, u"villager settings 'odd:gnome': 'unemployed' must name a profession without workstations; left out"_s));
        QVERIFY(anyContains(loaded.warnings, u"mob 'odd:gnome': no villager settings 'odd:gnome'"_s));
        const QcContentRegistry &content = *loaded.content;
        QVERIFY(!content.villageCatalog().villagerSettings(u"odd:gnome"_s));
        QVERIFY(!content.mobs()[*content.mobIndex(u"odd:gnome"_s)].villager);
        const int villager = *content.mobIndex(u"qtcraft:villager"_s);
        QCOMPARE(content.villagerSettingsOf(villager).name, u"qtcraft:villager"_s);
        QCOMPARE(content.mobs()[villager].villagerSettingsIndex, 0);
    }

    void aCreatureWithoutItsSettingsIsNoVillager()
    {
        QTemporaryDir dir;
        const QString pack = writePack(dir.path(), u"lonely"_s, {{u"mobs/gnome.json"_s, gnomeMob}});
        const Loaded loaded = load({pack});
        QVERIFY(anyContains(loaded.warnings, u"no villager settings 'lonely:gnome'; it is no villager and opens no doors"_s));
        const QcMobDefinition &gnome = loaded.content->mobs()[*loaded.content->mobIndex(u"lonely:gnome"_s)];
        QVERIFY(!gnome.villager);
        QCOMPARE(gnome.ai, QcMobDefinition::Ai::Passive);
        // It keeps the villager's other abilities (it still climbs ladders and keeps off deep jumps).
        QVERIFY(!gnome.abilities.opensDoors);
        QCOMPARE(gnome.abilities.climbs, QcMobAbilities::Climbing::Ladders);
        QVERIFY(!gnome.abilities.divesIntoWater);
    }

    void newerFormatsAreRefused()
    {
        QTemporaryDir dir;
        const QString pack = writePack(dir.path(), u"future"_s,
                                       {{u"professions/pilot.json"_s, R"({"formatVersion": 2, "name": "pilot"})"},
                                        {u"villages/sky.json"_s, R"({"formatVersion": 7, "name": "sky", "centre": "qtcraft:village_plains_well",
                                            "pieces": [{"structure": "qtcraft:village_plains_tower"}]})"},
                                        {u"villagers/robot.json"_s, R"({"formatVersion": 3, "name": "robot", "unemployed": "qtcraft:unemployed",
                                            "routine": [{"from": 0.0, "do": "work"}], "look": {"outfit": "qtcraft:villager_coat"}})"},
                                        {u"trades/pilot.json"_s, R"({"formatVersion": 2, "name": "pilot", "profession": "qtcraft:farmer", "offers": [
                                            {"level": 1, "costs": [{"item": "qtcraft:wheat"}], "result": {"item": "qtcraft:emerald"}}]})"},
                                        {u"worldgen/villages.json"_s, R"({"formatVersion": 2, "spacing": 128})"},
                                        {u"professions/odd.json"_s, R"({"formatVersion": 1.5, "name": "odd"})"}});
        const Loaded loaded = load({pack});
        const QcContentRegistry &content = *loaded.content;
        QCOMPARE(loaded.warnings.count(), 6);
        QCOMPARE(std::ranges::count_if(loaded.warnings, [](const QString &w) { return w.contains(u"is newer than this game reads (up to 1)"_s); }), 5);
        QVERIFY(anyContains(loaded.warnings, u"'formatVersion' must be a whole number"_s));
        QVERIFY(!content.villageCatalog().profession(u"future:pilot"_s));
        QVERIFY(!content.villageCatalog().style(u"future:sky"_s));
        QVERIFY(!content.villageCatalog().villagerSettings(u"future:robot"_s));
        QVERIFY(std::ranges::none_of(content.villageCatalog().tradeSets(), [](const QcTradeSet &set) { return set.name == u"future:pilot"_s; }));
        QCOMPARE(content.villageCatalog().settings().spacing, 384);
    }

    void filesOfTheEarlierFormatSayWhereTheirContentWent()
    {
        QTemporaryDir dir;
        const QString pack = writePack(dir.path(), u"old"_s,
                                       {{u"villagers.json"_s, R"({"mob": "villager"})"},
                                        {u"villages/old.json"_s, R"({"name": "old", "centre": "qtcraft:village_plains_well",
                                            "pieces": [{"structure": "qtcraft:village_plains_tower"}], "villagers": {"outfit": "qtcraft:villager_coat"}})"},
                                        {u"professions/cook.json"_s, R"({"name": "cook", "workstation": "cook"})"},
                                        {u"worldgen/villages.json"_s, R"({"workstations": {"furnace": "smith"}})"},
                                        {u"mobs/elder.json"_s, QByteArray(gnomeMob).replace(R"("villager": "gnome")", R"("villager": true)")}});
        const Loaded loaded = load({pack});
        QVERIFY(anyContains(loaded.warnings, u"moved to villagers/<name>.json and professions/<name>.json"_s));
        QVERIFY(anyContains(loaded.warnings, u"'villagers' (the look of the villagers) is now 'look'"_s));
        QVERIFY(anyContains(loaded.warnings, u"'workstation' moved to 'workstations'"_s));
        QVERIFY(anyContains(loaded.warnings, u"'workstations' moved to the professions"_s));
        QVERIFY(anyContains(loaded.warnings, u"'villager' names villager settings"_s));
        QCOMPARE(loaded.content->villageCatalog().settings().workstations.size(), 3);
    }
};

QTEST_GUILESS_MAIN(TestVillageMods)
#include "test_village_mods.moc"
