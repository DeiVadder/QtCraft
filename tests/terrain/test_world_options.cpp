// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/worldgen/QcWorldOptionParser.h"
#include "fixtures/QcTestUtil.h"
#include "server/core/QcServer.h"
#include "server/storage/QcWorldStorage.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worldgen/generators/QcWorldOptions.h"
#include "worldgen/terrain/QcTerrainGenerator.h"
#include "worldgen/villages/QcVillageGenerator.h"
#include "worldgen/villages/QcVillagePlan.h"
#include "worlds/QcModLoader.h"
#include "worlds/QcWorldLibrary.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

// The world generation options (decision E-917): the schema from the content, the values a world
// stores, and what they do to the generated world.
namespace {

const QString terrain = QString::fromLatin1(QcWorldGenerators::terrain);

// The chunks a comparison looks at: around the origin and one far away.
const QList<QcChunkPos> sampleChunks{{0, 0}, {1, 0}, {0, 1}, {-1, -1}, {37, -12}};

std::unique_ptr<QcWorldGenerator> generatorOf(const std::shared_ptr<const QcContentRegistry> &content, qint64 seed, const QJsonObject &values)
{
    const QcWorldOptions::Applied applied = QcWorldOptions::apply(content, terrain, values);
    return QcWorldGenerators::create(terrain, applied.content, seed, {}, QcGeneratorVersion::current);
}

bool sameChunk(const QcChunk &a, const QcChunk &b)
{
    for (int y = 0; y < QcWorld::worldHeight; ++y) {
        for (int z = 0; z < QcWorld::chunkSize; ++z) {
            for (int x = 0; x < QcWorld::chunkSize; ++x) {
                if (a.block(x, y, z) != b.block(x, y, z)) {
                    return false;
                }
            }
        }
    }
    return true;
}

// Whether both generators make the same blocks in every sample chunk.
bool sameWorld(const QcWorldGenerator &a, const QcWorldGenerator &b)
{
    return std::ranges::all_of(sampleChunks, [&](QcChunkPos pos) { return sameChunk(*a.generate(pos), *b.generate(pos)); });
}

// The first difference of two terrain generators in data that spans far more than the sample
// chunks: biome and climate on a 64-block grid over 4096 x 4096 blocks, the villages of 2 x 2 village
// regions, and the mine shafts, dungeons and coastal towers /locate finds within 768 blocks of 2 x 2
// points 1024 blocks apart. Empty when there is none; `places` counts what was found (so that the comparison
// is not of nothing).
struct LargeArea
{
    QString difference;
    int places = 0;
};

// The first sample of the 64-block grid over 4096 x 4096 blocks whose climate or biome differs.
QString climateDifference(const QcTerrainGenerator &a, const QcTerrainGenerator &b)
{
    for (int z = -2048; z < 2048; z += 64) {
        for (int x = -2048; x < 2048; x += 64) {
            const QcTerrainClimate::Point one = a.climateAt(x, z);
            const QcTerrainClimate::Point other = b.climateAt(x, z);
            const bool sameClimate = one.temperature == other.temperature && one.humidity == other.humidity
                                     && one.ruggedness == other.ruggedness && one.rarity == other.rarity;
            if (!sameClimate || a.biomeAt(x, z) != b.biomeAt(x, z)) {
                return u"climate or biome at %1 %2"_s.arg(x).arg(z);
            }
        }
    }
    return {};
}

bool sameVillage(const std::shared_ptr<const QcVillagePlan> &one, const std::shared_ptr<const QcVillagePlan> &other)
{
    if (!one || !other) {
        return !one && !other;
    }
    return one->centre == other->centre && one->style == other->style && one->pieces.size() == other->pieces.size();
}

LargeArea compareVillages(const QcTerrainGenerator &a, const QcTerrainGenerator &b)
{
    LargeArea result;
    const int spacing = a.villages().spacing();
    for (int i = -1; i <= 0; ++i) {
        for (int j = -1; j <= 0; ++j) {
            const QcChunkPos chunk{i * spacing / QcWorld::chunkSize, j * spacing / QcWorld::chunkSize};
            const std::shared_ptr<const QcVillagePlan> one = a.villageFor(chunk);
            if (!sameVillage(one, b.villageFor(chunk))) {
                return {u"village of chunk %1 %2"_s.arg(chunk.x).arg(chunk.z), 0};
            }
            result.places += one ? 1 : 0;
        }
    }
    return result;
}

LargeArea compareLocated(const QcTerrainGenerator &a, const QcTerrainGenerator &b, const QString &kind)
{
    LargeArea result;
    for (int i = -1; i <= 0; ++i) {
        for (int j = -1; j <= 0; ++j) {
            const QcBlockPos from{i * 1024 + 512, 64, j * 1024 + 512};
            const QcLocateResult one = a.locate(kind, from, 768);
            const QcLocateResult other = b.locate(kind, from, 768);
            if (one.place != other.place || one.searched != other.searched) {
                return {u"%1 from %2 %3"_s.arg(kind).arg(from.x).arg(from.z), 0};
            }
            result.places += one.place ? 1 : 0;
        }
    }
    return result;
}

LargeArea compareLargeArea(const QcWorldGenerator &a, const QcWorldGenerator &b)
{
    const auto *terrainA = dynamic_cast<const QcTerrainGenerator *>(&a);
    const auto *terrainB = dynamic_cast<const QcTerrainGenerator *>(&b);
    if (!terrainA || !terrainB) {
        return {u"not terrain generators"_s, 0};
    }
    if (const QString difference = climateDifference(*terrainA, *terrainB); !difference.isEmpty()) {
        return {difference, 0};
    }

    LargeArea total = compareVillages(*terrainA, *terrainB);
    for (const QString &kind : {u"mineshaft"_s, u"dungeon"_s, u"coastal_tower"_s}) {
        if (!total.difference.isEmpty()) {
            break;
        }
        const LargeArea located = compareLocated(*terrainA, *terrainB, kind);
        total = {located.difference, total.places + located.places};
    }
    return total;
}

// Blocks of the sample chunks whose name ends with "_ore", and air below y = 40.
struct Counts
{
    int ores = 0;
    int deepAir = 0;
};

Counts countsOf(const QcWorldGenerator &generator, const QcContentRegistry &content)
{
    Counts counts;
    for (const QcChunkPos pos : sampleChunks) {
        const std::shared_ptr<QcChunk> chunk = generator.generate(pos);
        for (int y = 1; y < QcWorld::worldHeight; ++y) {
            for (int z = 0; z < QcWorld::chunkSize; ++z) {
                for (int x = 0; x < QcWorld::chunkSize; ++x) {
                    const QcBlockId block = chunk->block(x, y, z);
                    counts.ores += content.block(block).name.endsWith(u"_ore"_s) ? 1 : 0;
                    counts.deepAir += y < 40 && block == qcAirBlockId ? 1 : 0;
                }
            }
        }
    }
    return counts;
}

// Neighbouring samples 64 blocks apart in a square of 64 x 64 samples that lie in different biomes.
int biomeBorders(const QcWorldGenerator &generator)
{
    int borders = 0;
    for (int i = 0; i < 64; ++i) {
        for (int j = 0; j + 1 < 64; ++j) {
            borders += generator.biomeAt(j * 64, i * 64) != generator.biomeAt((j + 1) * 64, i * 64) ? 1 : 0;
            borders += generator.biomeAt(i * 64, j * 64) != generator.biomeAt(i * 64, (j + 1) * 64) ? 1 : 0;
        }
    }
    return borders;
}

QJsonObject readJson(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject{};
}

} // namespace

class TestWorldOptions : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

    // The defaults with `changes` over them, as a new world stores them.
    QJsonObject valuesWith(const QJsonObject &changes) const
    {
        const QcResult<QJsonObject> values = QcWorldOptions::valuesToStore(terrain, changes, *m_content);
        return values ? *values : QJsonObject{};
    }

    // The fixed seeds and one drawn for the run (decision E-846).
    const QList<qint64> m_seeds{1, 20251028, QcTestUtil::randomSeed()};

private slots:
    void theBaseGameDefinesItsOptions()
    {
        const QList<QcWorldOptions::Option> options = QcWorldOptions::optionsOf(*m_content);
        QCOMPARE(options.size(), m_content->worldgen().worldOptions().size());
        QCOMPARE(options.size(), 9);
        QCOMPARE(QcWorldOptions::check(*m_content), QStringList());
        QCOMPARE(m_content->worldgen().worldOptionGroups().size(), 4);
        QCOMPARE(m_content->worldgen().worldPresets().size(), 4);

        // Every option, group and preset has its texts in English and German.
        QStringList keys;
        for (const QcWorldOptions::Option &option : options) {
            keys << option.definition->translationKey << option.definition->translationKey + u".help"_s;
        }
        for (const QcWorldOptionGroup &group : m_content->worldgen().worldOptionGroups()) {
            keys << group.translationKey;
        }
        for (const QcWorldPreset &preset : m_content->worldgen().worldPresets()) {
            keys << preset.translationKey << preset.translationKey + u".help"_s;
        }
        for (const QString &key : std::as_const(keys)) {
            QVERIFY2(m_content->translations().has(key, u"en"_s) && m_content->translations().has(key, u"de"_s), qPrintable(key));
        }
    }

    void theDefaultsAreTheContentValues()
    {
        const QJsonObject defaults = QcWorldOptions::defaults(*m_content);
        QCOMPARE(defaults.value(u"qtcraft:village_spacing"_s).toDouble(), m_content->villageCatalog().settings().spacing);
        QCOMPARE(defaults.value(u"qtcraft:village_chance"_s).toDouble(), m_content->villageCatalog().settings().chance);
        QCOMPARE(defaults.value(u"qtcraft:caves"_s), QJsonValue(true));
        QCOMPARE(defaults.value(u"qtcraft:rivers"_s), QJsonValue(true));
        QCOMPARE(defaults.value(u"qtcraft:biome_size"_s).toDouble(), 1.0);
        QCOMPARE(defaults.value(u"qtcraft:ore_amount"_s).toDouble(), 1.0);
        QCOMPARE(defaults.value(u"qtcraft:dungeons"_s).toDouble(), m_content->worldgen().terrainSettings().dungeons.chance);
        QCOMPARE(defaults.value(u"qtcraft:mineshafts"_s).toDouble(), m_content->worldgen().terrainSettings().mineshafts.chance);
        QCOMPARE(defaults.value(u"qtcraft:coastal_towers"_s).toDouble(), m_content->worldgen().terrainSettings().coastalTowers.chance);
        QCOMPARE(valuesWith({}), defaults);
    }

    // A world with every option at its default, and one without options (made before them), generate
    // exactly as the content alone does.
    void theDefaultsReproduceTodaysWorlds()
    {
        for (const qint64 seed : std::as_const(m_seeds)) {
            const auto plain = QcWorldGenerators::create(terrain, m_content, seed, {}, QcGeneratorVersion::current);
            QVERIFY(QcWorldOptions::apply(m_content, terrain, {}).content == m_content);
            const auto defaults = generatorOf(m_content, seed, QcWorldOptions::defaults(*m_content));
            QVERIFY2(sameWorld(*plain, *defaults), qPrintable(u"seed %1"_s.arg(seed)));
            const LargeArea area = compareLargeArea(*plain, *defaults);
            QVERIFY2(area.difference.isEmpty(), qPrintable(u"seed %1: %2"_s.arg(seed).arg(area.difference)));
            qInfo("seed %lld: %d villages and structures compared", static_cast<long long>(seed), area.places);
            QVERIFY(area.places > 0);
        }

        // The comparison sees a change of a single option far away.
        const auto plain = QcWorldGenerators::create(terrain, m_content, 1, {}, QcGeneratorVersion::current);
        QVERIFY(!compareLargeArea(*plain, *generatorOf(m_content, 1, valuesWith({{u"qtcraft:biome_size"_s, 1.25}}))).difference.isEmpty());
        QVERIFY(!compareLargeArea(*plain, *generatorOf(m_content, 1, valuesWith({{u"qtcraft:dungeons"_s, 0.3}}))).difference.isEmpty());
    }

    // A slider's inexact value lands on the option's grid, its default exactly on the default.
    void numbersSnapToTheGrid()
    {
        const QList<QcWorldOptions::Option> options = QcWorldOptions::optionsOf(*m_content);
        const auto option = [&options](const QString &name) {
            return *std::ranges::find(options, name, [](const QcWorldOptions::Option &each) { return each.definition->name; });
        };
        QCOMPARE(option(u"qtcraft:dungeons"_s).snapped(0.65 - 0.05), 0.6);
        QCOMPARE(option(u"qtcraft:dungeons"_s).snapped(0.6000000000000001), option(u"qtcraft:dungeons"_s).defaultValue);
        QCOMPARE(option(u"qtcraft:dungeons"_s).snapped(0.62), 0.6);
        QCOMPARE(option(u"qtcraft:dungeons"_s).snapped(0.1 + 0.2), 0.3);
        QCOMPARE(option(u"qtcraft:biome_size"_s).snapped(2.9999999999999996), 3.0);
        QCOMPARE(option(u"qtcraft:village_spacing"_s).snapped(200.0), 192.0);
        QCOMPARE(option(u"qtcraft:village_spacing"_s).snapped(2000.0), 2016.0);
        QCOMPARE(option(u"qtcraft:caves"_s).snapped(1.0), 1.0);
    }

    void theSameSeedAndOptionsMakeTheSameWorld()
    {
        const QJsonObject values = valuesWith({{u"qtcraft:caves"_s, false}, {u"qtcraft:ore_amount"_s, 2.0}, {u"qtcraft:biome_size"_s, 2.0}});
        for (const qint64 seed : std::as_const(m_seeds)) {
            QVERIFY(sameWorld(*generatorOf(m_content, seed, values), *generatorOf(m_content, seed, values)));
            QVERIFY(!sameWorld(*generatorOf(m_content, seed, values), *generatorOf(m_content, seed, {})));
        }
    }

    void cavesAndOresFollowTheirOptions()
    {
        const qint64 seed = 1;
        const Counts usual = countsOf(*generatorOf(m_content, seed, {}), *m_content);
        const QJsonObject noCaves = valuesWith({{u"qtcraft:caves"_s, false}, {u"qtcraft:dungeons"_s, 0.0}, {u"qtcraft:mineshafts"_s, 0.0}});
        const Counts closed = countsOf(*generatorOf(m_content, seed, noCaves), *m_content);
        qInfo("deep air %d without caves, %d with them; ore blocks %d", closed.deepAir, usual.deepAir, usual.ores);
        QVERIFY(usual.deepAir > 0);
        QVERIFY(closed.deepAir < usual.deepAir / 4);

        QCOMPARE(countsOf(*generatorOf(m_content, seed, valuesWith({{u"qtcraft:ore_amount"_s, 0.0}})), *m_content).ores, 0);
        QVERIFY(countsOf(*generatorOf(m_content, seed, valuesWith({{u"qtcraft:ore_amount"_s, 3.0}})), *m_content).ores > usual.ores * 2);
    }

    void largerBiomesHaveFewerBorders()
    {
        for (const qint64 seed : std::as_const(m_seeds)) {
            const int usual = biomeBorders(*generatorOf(m_content, seed, {}));
            const int large = biomeBorders(*generatorOf(m_content, seed, valuesWith({{u"qtcraft:biome_size"_s, 3.0}})));
            qInfo("seed %lld: %d biome borders, %d with biomes three times as large", static_cast<long long>(seed), usual, large);
            QVERIFY(large < usual * 2 / 3);
        }
    }

    // The values reach the generator's parts through the content: villages, structures, rivers.
    void theOptionsSetTheGeneratorValues()
    {
        const QJsonObject values = valuesWith({{u"qtcraft:village_spacing"_s, 256.0},
                                               {u"qtcraft:village_chance"_s, 0.0},
                                               {u"qtcraft:rivers"_s, false},
                                               {u"qtcraft:dungeons"_s, 0.25},
                                               {u"qtcraft:mineshafts"_s, 0.5},
                                               {u"qtcraft:coastal_towers"_s, 1.0}});
        const QcWorldOptions::Applied applied = QcWorldOptions::apply(m_content, terrain, values);
        QCOMPARE(applied.warnings, QStringList());
        const QcTerrainSettings &settings = applied.content->worldgen().terrainSettings();
        QCOMPARE(applied.content->villageCatalog().settings().spacing, 256);
        QCOMPARE(applied.content->villageCatalog().settings().chance, 0.0);
        QVERIFY(!settings.rivers && !settings.deltas);
        QCOMPARE(settings.dungeons.chance, 0.25);
        QCOMPARE(settings.mineshafts.chance, 0.5);
        QCOMPARE(settings.coastalTowers.chance, 1.0);
        // The shared content keeps its own values.
        QCOMPARE(m_content->villageCatalog().settings().spacing, 384);
        QVERIFY(m_content->worldgen().terrainSettings().rivers);

        // Without villages, no region has one.
        const auto generator = generatorOf(m_content, 7, values);
        for (int x = -1; x <= 1; ++x) {
            for (int z = -1; z <= 1; ++z) {
                QVERIFY(!generator->villageFor({x * 16, z * 16}));
            }
        }
    }

    void presetsSetTheirValues()
    {
        const QcResult<QJsonObject> more = QcWorldOptions::presetValues(u"more_villages"_s, *m_content);
        QVERIFY(more);
        QCOMPARE(more->value(u"qtcraft:village_spacing"_s).toDouble(), 256.0);
        QCOMPARE(more->value(u"qtcraft:village_chance"_s).toDouble(), 0.9);
        QCOMPARE(more->value(u"qtcraft:caves"_s), QJsonValue(true));
        QCOMPARE(QcWorldOptions::presetOf(*more, *m_content), u"qtcraft:more_villages"_s);
        QCOMPARE(QcWorldOptions::presetOf(QcWorldOptions::defaults(*m_content), *m_content), QString());
        QVERIFY(!QcWorldOptions::presetValues(u"no_such_preset"_s, *m_content));
    }

    void storedValuesAreChecked()
    {
        QVERIFY(QcWorldOptions::valuesToStore(terrain, {{u"qtcraft:village_spacing"_s, 512.0}}, *m_content));
        // Unknown options, values outside the range or of the wrong kind are refused.
        QVERIFY(!QcWorldOptions::valuesToStore(terrain, {{u"qtcraft:nothing"_s, 1.0}}, *m_content));
        QVERIFY(!QcWorldOptions::valuesToStore(terrain, {{u"qtcraft:village_spacing"_s, 64.0}}, *m_content));
        QVERIFY(!QcWorldOptions::valuesToStore(terrain, {{u"qtcraft:caves"_s, 1.0}}, *m_content));
        QVERIFY(!QcWorldOptions::valuesToStore(terrain, {{u"qtcraft:biome_size"_s, u"large"_s}}, *m_content));
        // Other world types store none and take none.
        QCOMPARE(*QcWorldOptions::valuesToStore(u"flat"_s, {}, *m_content), QJsonObject());
        QVERIFY(!QcWorldOptions::valuesToStore(u"flat"_s, {{u"qtcraft:caves"_s, false}}, *m_content));
        QVERIFY(QcWorldOptions::apply(m_content, u"flat"_s, valuesWith({{u"qtcraft:caves"_s, false}})).content == m_content);
    }

    void theCommandLineNamesOptionsAndPresets()
    {
        const QcResult<QJsonObject> values = QcWorldOptions::parseText(u"preset=more_villages, caves=off,qtcraft:ore_amount=1.5"_s, *m_content);
        QVERIFY2(values, qcErrorText(values).constData());
        QCOMPARE(values->size(), 4);
        QCOMPARE(values->value(u"qtcraft:village_spacing"_s).toDouble(), 256.0);
        QCOMPARE(values->value(u"qtcraft:caves"_s), QJsonValue(false));
        QCOMPARE(values->value(u"qtcraft:ore_amount"_s).toDouble(), 1.5);
        QCOMPARE(*QcWorldOptions::parseText({}, *m_content), QJsonObject());

        QVERIFY(!QcWorldOptions::parseText(u"preset=nothing"_s, *m_content));
        QVERIFY(!QcWorldOptions::parseText(u"caves=maybe"_s, *m_content));
        QVERIFY(!QcWorldOptions::parseText(u"ore_amount=lots"_s, *m_content));
        QVERIFY(!QcWorldOptions::parseText(u"lakes=on"_s, *m_content));

        // Presets apply first: a value named explicitly wins wherever it stands.
        for (const QString &text : {u"village_chance=0.5,preset=more_villages"_s, u"preset=more_villages,village_chance=0.5"_s}) {
            const QcResult<QJsonObject> ordered = QcWorldOptions::parseText(text, *m_content);
            QVERIFY2(ordered, qcErrorText(ordered).constData());
            QCOMPARE(ordered->value(u"qtcraft:village_chance"_s).toDouble(), 0.5);
            QCOMPARE(ordered->value(u"qtcraft:village_spacing"_s).toDouble(), 256.0);
        }
    }

    // Values the content does not know (a mod gone) or cannot take are left out with a warning.
    void unknownOrBrokenValuesAreIgnored()
    {
        QJsonObject values = valuesWith({});
        values.insert(u"gone:option"_s, 3.0);
        values.insert(u"qtcraft:caves"_s, 5.0);
        const QcWorldOptions::Applied applied = QcWorldOptions::apply(m_content, terrain, values);
        QCOMPARE(applied.warnings.size(), 2);
        QVERIFY(applied.content->worldgen().terrainSettings().caves);
    }

    void packsAddOptionsAndPresets()
    {
        const QcResult<QcWorldOptionSet> set = QcWorldOptionParser::parseOptionSet(
            QJsonDocument::fromJson(R"({"groups": [{"name": "extras"}],
                "options": [{"name": "diamonds", "group": "extras", "target": "ore.amount", "subject": "qtcraft:diamond", "unit": "factor", "min": 0, "max": 4},
                            {"name": "broken", "group": "extras", "target": "terrain.lakes"},
                            {"name": "far", "group": "qtcraft:villages", "target": "villages.chance", "min": 0, "max": 2}],
                "presets": [{"name": "no_diamonds", "values": {"diamonds": 0, "missing": 1}}]})")
                .object(),
            u"extra"_s);
        QVERIFY2(set, qcErrorText(set).constData());
        auto content = std::make_shared<QcContentRegistry>(*m_content);
        content->worldgen().addWorldOptions(*set);

        // The unknown target and the range beyond the target's are left out with warnings.
        QCOMPARE(QcWorldOptions::optionsOf(*content).size(), 10);
        QCOMPARE(QcWorldOptions::check(*content).size(), 3);

        const QcResult<QJsonObject> values = QcWorldOptions::presetValues(u"extra:no_diamonds"_s, *content);
        QVERIFY(values);
        const QcWorldOptions::Applied applied = QcWorldOptions::apply(content, terrain, *values);
        for (const QcOreDefinition &ore : applied.content->worldgen().ores()) {
            const auto original = std::ranges::find(m_content->worldgen().ores(), ore.name, &QcOreDefinition::name);
            QCOMPARE(ore.veinsPerChunk, ore.name == u"qtcraft:diamond"_s ? 0 : original->veinsPerChunk);
        }

        QVERIFY(!QcWorldOptionParser::parseOptionSet(QJsonDocument::fromJson(R"({"options": [{"name": "x", "group": "g"}]})").object(), u"extra"_s));
        QVERIFY(!QcWorldOptionParser::parseOptionSet(QJsonDocument::fromJson(R"({"options": [{"name": "x", "group": "g", "target": "t", "unit": "miles"}]})").object(),
                                                     u"extra"_s));
    }

    void theExampleModAddsItsRubyOption()
    {
        const QcModLoader::Loaded loaded = QcModLoader::load(QStringLiteral(QTCRAFT_MODS_SOURCE_DIR));
        QVERIFY(loaded);
        const QcWorldOptionDefinition *ruby = loaded->content->worldgen().worldOption(u"rubies:ruby_amount"_s);
        QVERIFY(ruby);
        QCOMPARE(ruby->subject, u"rubies:ruby"_s);
        QCOMPARE(QcWorldOptions::check(*loaded->content), QStringList());
        const QcWorldOptions::Applied applied = QcWorldOptions::apply(loaded->content, terrain, {{u"rubies:ruby_amount"_s, 0.0}});
        const QList<QcOreDefinition> &ores = applied.content->worldgen().ores();
        QCOMPARE(std::ranges::find(ores, u"rubies:ruby"_s, &QcOreDefinition::name)->veinsPerChunk, 0);
    }

    // world.json keeps the values; the world opens with them, also after the content's defaults
    // changed, and a world without them (format 17) still opens.
    void worldsKeepTheirOptions()
    {
        QTemporaryDir directory;
        const QcWorldLibrary library(directory.path());
        const QJsonObject values = valuesWith({{u"qtcraft:village_spacing"_s, 512.0}, {u"qtcraft:rivers"_s, false}});
        const QcResult<QcWorldLibrary::Entry> created =
            library.create(u"Options"_s, 5, {.generator = terrain, .options = {}, .worldOptions = values, .gameMode = QcGameMode::Survival});
        QVERIFY(created);
        const QJsonObject json = readJson(QDir(created->directory).filePath(u"world.json"_s));
        QCOMPARE(json.value(u"formatVersion"_s).toInt(), QcWorldInfo::formatVersion);
        QCOMPARE(json.value(u"generator"_s).toObject().value(u"worldOptions"_s).toObject(), values);
        const QList<QcWorldLibrary::Entry> listed = library.list();
        QCOMPARE(listed.size(), 1);
        QCOMPARE(listed.first().info.worldOptions, values);

        // An update makes villages rarer by default: the world keeps its own value.
        auto changed = std::make_shared<QcContentRegistry>(*m_content);
        QcVillageSettings villages = changed->villageCatalog().settings();
        villages.spacing = 768;
        changed->villageCatalog().setSettings(villages);
        QCOMPARE(QcWorldOptions::apply(changed, terrain, listed.first().info.worldOptions).content->villageCatalog().settings().spacing, 512);

        QcServerConfig config;
        config.worldDirectory = created->directory;
        config.seed = 5;
        config.worldOptions = values;
        const QcWorldOptions::Applied applied = QcWorldOptions::apply(m_content, terrain, values);
        {
            QcServer server(config, applied.content, QcWorldGenerators::create(terrain, applied.content, 5, {}, QcGeneratorVersion::current));
            QVERIFY(server.openWorld());
            QVERIFY(server.stop());
        }
        // Opened with other options, the world refuses.
        config.worldOptions = valuesWith({{u"qtcraft:village_spacing"_s, 512.0}});
        QcServer other(config, m_content, QcWorldGenerators::create(terrain, m_content, 5, {}, QcGeneratorVersion::current));
        const QcStatus refused = other.openWorld();
        QVERIFY(!refused);
        // The refusal names the first option that differs, with both values.
        QVERIFY2(qcErrorText(refused).contains("'qtcraft:rivers' = false, not true"), qcErrorText(refused).constData());
    }

    void worldsWithoutOptionsOpenAsBefore()
    {
        QTemporaryDir directory;
        const QcWorldLibrary library(directory.path());
        const QcResult<QcWorldLibrary::Entry> created = library.create(u"Plain"_s, 9);
        QVERIFY(created);
        const QString path = QDir(created->directory).filePath(u"world.json"_s);
        QJsonObject json = readJson(path);
        QVERIFY(!json.value(u"generator"_s).toObject().contains(u"worldOptions"_s));

        // As an older version wrote it.
        json.insert(u"formatVersion"_s, 17);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QJsonDocument(json).toJson());
        file.close();
        const QcResult<QcWorldInfo> info = QcWorldStorage(created->directory, nullptr).readInfo();
        QVERIFY2(info, qcErrorText(info).constData());
        QCOMPARE(info->worldOptions, QJsonObject());

        QcServerConfig config;
        config.worldDirectory = created->directory;
        config.seed = 9;
        QcServer server(config, m_content, QcWorldGenerators::create(terrain, m_content, 9, {}, QcGeneratorVersion::current));
        QVERIFY(server.openWorld());
        QVERIFY(server.stop());
    }
};

QTEST_GUILESS_MAIN(TestWorldOptions)
#include "test_world_options.moc"
