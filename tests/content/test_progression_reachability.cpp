// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/registry/QcContentParser.h"
#include "content/QcProgressionRules.h"
#include "content/QcWorldCensus.h"
#include "fixtures/QcTestUtil.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worldgen/villages/QcVillagePlan.h"
#include "worlds/QcModLoader.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <array>
#include <memory>

using namespace Qt::StringLiterals;

// Whether the goals of the content can be reached from every start dimension (decisions E-1095 to
// E-1097): the rules of the real content over censuses of really generated chunks. Seeds as in
// decision E-846: fixed ones plus a random one per run, in the log; QC_TEST_SEED sets it. With
// QC_TEST_MOD_DIR set to a mods folder, the base game and its mods are checked, a mod's goals too.
namespace {

namespace Node = QcProgressionNode;

constexpr std::array<qint64, 3> fixedSeeds{42, 1, 20261106};

const QString overworld = QString::fromLatin1(QcDimensionDefinition::overworldName);
const QString nether = u"qtcraft:nether"_s;
// The blocks of the smallest frame of the nether portal, 4 x 5 without corners.
constexpr int frameBlocks = 10;

// The censuses of the dimensions of one world.
struct WorldSample
{
    qint64 seed = 0;
    QList<QcWorldCensus> censuses;
};

[[nodiscard]] const QcProgressionResult *resultFor(const QList<QcProgressionResult> &results, const QString &goal)
{
    const auto found = std::ranges::find_if(results, [&goal](const QcProgressionResult &result) { return result.goal == goal; });
    return found == results.cend() ? nullptr : &*found;
}

// A census of `dimension` that holds `nodes`, a hundred of each.
[[nodiscard]] QcWorldCensus censusOf(const QString &dimension, const QStringList &nodes)
{
    QcWorldCensus census{dimension, {}};
    for (const QString &node : nodes) {
        census.found.insert(node, 100);
    }
    return census;
}

// The land of an overworld with what the goals need, without any structure, so that no chest or
// village furnace opens a second way around a mutation.
[[nodiscard]] QcWorldCensus plainOverworld()
{
    return censusOf(overworld, {Node::block(u"qtcraft:oak_log"_s), Node::block(u"qtcraft:stone"_s), Node::block(u"qtcraft:coal_ore"_s),
                                Node::block(u"qtcraft:iron_ore"_s), Node::block(u"qtcraft:diamond_ore"_s), Node::block(u"qtcraft:gravel"_s),
                                Node::block(u"qtcraft:water"_s), Node::block(u"qtcraft:lava"_s), Node::creature(u"qtcraft:pig"_s)});
}

[[nodiscard]] QList<QcProgressionResult> checkPlainOverworld(const QcContentRegistry &content)
{
    return QcProgressionRules::check(content, QcProgressionRules::ofContent(content), {plainOverworld()}, overworld);
}

} // namespace

class TestProgressionReachability : public QObject
{
    Q_OBJECT

private:
    [[nodiscard]] WorldSample sampleOf(qint64 seed) const
    {
        const QcWorldCensus::Area area;
        WorldSample sample{seed, {}};
        const auto terrain = QcWorldGenerators::create(QString::fromLatin1(QcWorldGenerators::terrain), m_content, seed, {}, QcGeneratorVersion::current);
        sample.censuses.append(QcWorldCensus::take(*terrain, m_content->worldgen().overworld(), *m_content, area));
        const QcDimensionDefinition *dimension = m_content->worldgen().dimension(nether);
        const auto generator = QcWorldGenerators::createDimension(*dimension, m_content, seed, QcWorldGenerators::versionForNewWorld(*dimension));
        sample.censuses.append(QcWorldCensus::take(*generator, *dimension, *m_content, area));
        return sample;
    }

    [[nodiscard]] QList<QcProgressionResult> resultsOf(const WorldSample &sample, const QString &start) const
    {
        return QcProgressionRules::check(*m_content, m_rules, sample.censuses, start);
    }

    // The base game alone, for the cases that change it or make up their own land; m_content and
    // m_rules add the mods of QC_TEST_MOD_DIR.
    std::shared_ptr<QcContentRegistry> m_base = QcTestUtil::loadContent();
    QList<QcProgressionRule> m_baseRules = QcProgressionRules::ofContent(*m_base);
    std::shared_ptr<QcContentRegistry> m_content = m_base;
    QList<QcProgressionRule> m_rules;
    QList<WorldSample> m_samples;

private slots:
    void initTestCase()
    {
        const QString mods = qEnvironmentVariable("QC_TEST_MOD_DIR");
        if (!mods.isEmpty()) {
            const QcModLoader::Loaded loaded = QcModLoader::load(mods);
            QVERIFY2(loaded.has_value(), qPrintable(QcModLoader::warningsOf(loaded).join(u'\n')));
            for (const QcModStatus &mod : loaded->mods) {
                QVERIFY2(mod.state == QcModStatus::State::Loaded, qPrintable(mod.reason));
                QVERIFY2(mod.warnings.isEmpty(), qPrintable(mod.warnings.join(u'\n')));
            }
            m_content = loaded->content;
        }

        m_rules = QcProgressionRules::ofContent(*m_content);
        for (const qint64 seed : fixedSeeds) {
            m_samples.append(sampleOf(seed));
        }
        m_samples.append(sampleOf(QcTestUtil::randomSeed()));
    }

    // Every goal names what exists, and the key goals of the plan are there.
    void goalsNameWhatExists()
    {
        QStringList names;
        for (const QcProgressionGoal &goal : m_content->worldgen().progressionGoals()) {
            names.append(goal.name);
            if (goal.kind == QcProgressionGoal::Kind::Item) {
                QVERIFY2(m_content->itemId(goal.target).has_value(), qPrintable(goal.name));
            } else if (goal.kind == QcProgressionGoal::Kind::Portal) {
                QVERIFY2(m_content->worldgen().portal(goal.target), qPrintable(goal.name));
            } else if (goal.kind == QcProgressionGoal::Kind::Dimension) {
                QVERIFY2(m_content->worldgen().dimension(goal.target), qPrintable(goal.name));
            }
        }

        for (const char *name : {"crafting_table", "wooden_pickaxe", "stone_pickaxe", "furnace", "fire", "food", "obsidian", "nether_portal", "overworld"}) {
            QVERIFY2(names.contains(u"qtcraft:"_s + QString::fromLatin1(name)), name);
        }
    }

    // The census is of real worlds: each dimension has its own ground and its own creatures. What a
    // sample may miss by chance (a biome, a structure) is asked of the fixed seeds together.
    void censusSeesTheGenerators()
    {
        for (const WorldSample &sample : std::as_const(m_samples)) {
            const QcWorldCensus &land = sample.censuses.at(0);
            const QcWorldCensus &below = sample.censuses.at(1);
            QVERIFY(land.found.value(Node::block(u"qtcraft:stone"_s)) > 1000);
            QVERIFY(!land.found.contains(Node::block(u"qtcraft:nether_gold_ore"_s)));
            QVERIFY(below.found.value(Node::block(u"qtcraft:netherrack"_s)) > 1000);
            QVERIFY(!below.found.contains(Node::block(u"qtcraft:stone"_s)));
            QVERIFY(!below.found.contains(Node::creature(u"qtcraft:cow"_s)));
        }

        const auto fixedSamples = m_samples.first(std::ssize(fixedSeeds));
        QVERIFY(std::ranges::any_of(fixedSamples, [](const WorldSample &sample) { return sample.censuses.at(0).found.contains(Node::creature(u"qtcraft:cow"_s)); }));
        QVERIFY(std::ranges::any_of(fixedSamples, [](const WorldSample &sample) { return sample.censuses.at(0).found.contains(Node::villagers()); }));
    }

    // The spawn samples of the fixed seeds hold no chest of the nether, so the census goes to a
    // fortress: its chests, spawners and spawn area are found by what the generator furnishes, which
    // no block of a chunk shows.
    void censusSeesTheFurnishingOfAFortress()
    {
        const QcDimensionDefinition *dimension = m_content->worldgen().dimension(nether);
        const auto generator = QcWorldGenerators::createDimension(*dimension, m_content, fixedSeeds.front(), QcWorldGenerators::versionForNewWorld(*dimension));
        const QcLocateResult fortress = generator->locate(u"fortress", generator->spawnPoint(), 4000);
        QVERIFY(fortress.place.has_value());

        const QcWorldCensus census = QcWorldCensus::take(*generator, *dimension, *m_content, {8, 1, fortress.place->at});
        QVERIFY(census.found.value(Node::chest(u"qtcraft:fortress_chest"_s)) >= 2);
        QVERIFY(census.found.contains(Node::creature(u"qtcraft:ember_wraith"_s)));
        QVERIFY(census.found.contains(Node::creature(u"qtcraft:charred_skeleton"_s)));
    }

    // A village's chests come from the generator's furnishing as well: those its pieces gain in a
    // later generator version (generatorChanges[].loot) and those its style renames (the style's
    // loot), so the census reads them where a village stands.
    void censusSeesTheChestsOfAVillage()
    {
        const QcDimensionDefinition &dimension = m_content->worldgen().overworld();
        const auto terrain = QcWorldGenerators::create(QString::fromLatin1(QcWorldGenerators::terrain), m_content, fixedSeeds.front(), {}, QcGeneratorVersion::current);
        QVERIFY(terrain);
        const std::shared_ptr<const QcVillagePlan> village = terrain->nearestVillage(terrain->spawnPoint(), 4000);
        QVERIFY(village);

        const QcWorldCensus census = QcWorldCensus::take(*terrain, dimension, *m_content, {4, 1, village->centre});
        QStringList chests;
        for (auto it = census.found.cbegin(); it != census.found.cend(); ++it) {
            if (it.key().startsWith(Node::chest(u"qtcraft:village_"_s))) {
                chests.append(it.key());
            }
        }
        qInfo("%s", qPrintable(chests.join(u", "_s)));
        QVERIFY(!chests.isEmpty());
    }

    void overworldStartReachesEveryGoal()
    {
        for (const WorldSample &sample : std::as_const(m_samples)) {
            const QList<QcProgressionResult> results = resultsOf(sample, overworld);
            for (const QcProgressionResult &result : results) {
                QVERIFY2(result.met, qPrintable(u"seed %1, %2"_s.arg(sample.seed).arg(result.describe())));
            }
        }
    }

    // The way of the plan without water and iron ore, inside the nether alone: without the census of
    // the overworld nothing can come from there. The log shows each goal's way.
    void netherStartReachesEveryGoal()
    {
        for (const WorldSample &sample : std::as_const(m_samples)) {
            const QList<QcProgressionResult> results = QcProgressionRules::check(*m_content, m_rules, {sample.censuses.at(1)}, nether);
            for (const QcProgressionResult &result : results) {
                qInfo("seed %lld %s", sample.seed, qPrintable(result.describe()));
                QVERIFY2(result.met, qPrintable(u"seed %1, %2"_s.arg(sample.seed).arg(result.describe())));
            }
        }
    }

    // Mutation of real nethers: without tusklings there is nothing to barter with, and obsidian for
    // the frame hangs on them, unless the chests hold enough of it.
    void theWayOutOfTheNetherHangsOnBartering()
    {
        const QString tuskling = Node::creature(u"qtcraft:tuskling"_s);
        int stopped = 0;
        for (const WorldSample &sample : std::as_const(m_samples)) {
            QcWorldCensus below = sample.censuses.at(1);
            if (!below.found.remove(tuskling)) {
                continue; // The sample has none: netherStartReachesEveryGoal speaks for this seed.
            }

            QList<QcProgressionRule> rules = m_rules + below.rules();
            const QcProgressionSolver solver(std::move(rules), {Node::dimension(nether)});
            const QString home = Node::dimension(overworld);
            if (solver.supply(Node::item(u"qtcraft:obsidian"_s)) < frameBlocks) {
                QVERIFY(!solver.reached(home));
                QVERIFY2(solver.gapTo(home).missing.contains(tuskling), qPrintable(solver.gapTo(home).missing.join(u", "_s)));
                ++stopped;
            }
        }
        QVERIFY2(stopped > 0, "every sample has a chest with obsidian: the fixed seeds no longer show this");
    }

    // The plain land reaches every goal: the mutations below start from a green graph.
    void plainLandReachesEveryGoal()
    {
        const QList<QcProgressionResult> results = checkPlainOverworld(*m_base);
        for (const QcProgressionResult &result : results) {
            QVERIFY2(result.met, qPrintable(result.describe()));
        }
    }

    // Mutation: without the recipe of the table nothing gives it, and that is what the report says;
    // the furnace behind it names the same step at the end of its way.
    void aMissingRecipeIsNamed()
    {
        QcContentRegistry content = *m_base;
        for (QcRecipeDefinition recipe : m_base->recipes()) {
            if (recipe.resultItem == u"qtcraft:crafting_table"_s) {
                recipe.resultItem = u"qtcraft:stick"_s;
                content.registerRecipe(recipe);
            }
        }

        const QList<QcProgressionResult> results = checkPlainOverworld(content);
        const QcProgressionResult *table = resultFor(results, u"qtcraft:crafting_table"_s);
        QVERIFY(table);
        QVERIFY(!table->met);
        QVERIFY2(table->missing.join(u'\n').contains(Node::item(u"qtcraft:crafting_table"_s)), qPrintable(table->describe()));
        const QcProgressionResult *furnace = resultFor(results, u"qtcraft:furnace"_s);
        QVERIFY(furnace);
        QVERIFY(!furnace->met);
        QVERIFY2(furnace->missing.join(u'\n').contains(Node::item(u"qtcraft:crafting_table"_s)), qPrintable(furnace->describe()));
    }

    // Mutation: stone that drops nothing leaves the stone pickaxe without its material, and the
    // report names the material and what else would give one (blackstone from a tuskling).
    void aMissingDropIsNamed()
    {
        QcContentRegistry content = *m_base;
        QcBlockDefinition stone = content.block(*content.blockId(u"qtcraft:stone"_s));
        stone.drops.clear();
        QVERIFY(content.registerBlock(stone).has_value());

        QcWorldCensus land = plainOverworld();
        land.found.remove(Node::block(u"qtcraft:lava"_s)); // Lava meeting water would make cobblestone.
        const QList<QcProgressionResult> results = QcProgressionRules::check(content, QcProgressionRules::ofContent(content), {land}, overworld);
        const QcProgressionResult *pickaxe = resultFor(results, u"qtcraft:stone_pickaxe"_s);
        QVERIFY(pickaxe);
        QVERIFY(!pickaxe->met);
        QVERIFY2(pickaxe->path.contains(u"tag qtcraft:stone_crafting_materials"_s), qPrintable(pickaxe->describe()));
        QVERIFY2(pickaxe->missing.contains(Node::creature(u"qtcraft:tuskling"_s)), qPrintable(pickaxe->describe()));
        const QcProgressionResult *wood = resultFor(results, u"qtcraft:wooden_pickaxe"_s);
        QVERIFY(wood);
        QVERIFY(wood->met);
    }

    // Mutation: with no pickaxe above level 2 the tool itself is the missing step.
    void aMissingToolIsNamed()
    {
        QcContentRegistry content = *m_base;
        for (QcItemDefinition item : m_base->items()) {
            if (item.tool == u"pickaxe"_s && item.toolLevel > 2) {
                item.toolLevel = 2;
                QVERIFY(content.registerItem(item).has_value());
            }
        }

        const QList<QcProgressionResult> results = checkPlainOverworld(content);
        const QcProgressionResult *obsidian = resultFor(results, u"qtcraft:obsidian"_s);
        QVERIFY(obsidian);
        QVERIFY(!obsidian->met);
        QVERIFY2(obsidian->missing.contains(u"pickaxe of level 3"_s), qPrintable(obsidian->describe()));
    }

    // Mutation: land without any wood stops at the first goal, and names the logs that are not there.
    void aMissingBlockIsNamed()
    {
        QcWorldCensus land = plainOverworld();
        land.found.remove(Node::block(u"qtcraft:oak_log"_s));
        const QList<QcProgressionResult> results = QcProgressionRules::check(*m_base, m_baseRules, {land}, overworld);
        const QcProgressionResult *table = resultFor(results, u"qtcraft:crafting_table"_s);
        QVERIFY(table);
        QVERIFY(!table->met);
        QVERIFY2(table->missing.join(u'\n').contains(Node::block(u"qtcraft:birch_log"_s)), qPrintable(table->describe()));
        QVERIFY2(table->path.contains(u"tag qtcraft:planks"_s), qPrintable(table->describe()));
    }

    // Three blocks are not ten: a goal with a count looks at how much there is.
    void aCountAsksForEnough()
    {
        QcWorldCensus land = plainOverworld();
        land.found.remove(Node::block(u"qtcraft:lava"_s));
        land.found.insert(Node::block(u"qtcraft:obsidian"_s), 3);
        const auto obsidianOf = [this](const QcWorldCensus &census) {
            const QList<QcProgressionResult> results = QcProgressionRules::check(*m_base, m_baseRules, {census}, overworld);
            return *resultFor(results, u"qtcraft:obsidian"_s);
        };

        const QcProgressionResult few = obsidianOf(land);
        QVERIFY(!few.met);
        QCOMPARE(few.missing, QStringList{u"7 of 10 item qtcraft:obsidian"_s});

        land.found.insert(Node::block(u"qtcraft:obsidian"_s), 10);
        QVERIFY(obsidianOf(land).met);
    }

    // What lies behind a portal opens only when it is lit.
    void aPortalOpensTheDimensionBehindIt()
    {
        const QString behind = Node::block(u"qtcraft:netherrack"_s);
        const QcWorldCensus below = censusOf(nether, {behind});
        const QcProgressionSolver open(m_baseRules + plainOverworld().rules() + below.rules(), {Node::dimension(overworld)});
        QVERIFY(open.reached(behind));
        QVERIFY(open.reached(Node::portal(u"qtcraft:nether"_s)));

        QcWorldCensus land = plainOverworld();
        land.found.remove(Node::block(u"qtcraft:lava"_s));
        const QcProgressionSolver closed(m_baseRules + land.rules() + below.rules(), {Node::dimension(overworld)});
        QVERIFY(!closed.reached(behind));
        const QStringList path = closed.gapTo(Node::dimension(nether)).path;
        QVERIFY2(path.contains(u"lighting portal qtcraft:nether in qtcraft:overworld"_s), qPrintable(path.join(u" <- "_s)));
    }

    // Nine blocks are no frame: the portal counts its blocks, whatever a single chest gives.
    void aPortalNeedsAWholeFrame()
    {
        const QString obsidian = Node::block(u"qtcraft:obsidian"_s);
        const QString lit = Node::portal(u"qtcraft:nether"_s);
        QcWorldCensus land = plainOverworld();
        land.found.remove(Node::block(u"qtcraft:lava"_s));
        land.found.insert(obsidian, frameBlocks - 1);
        const QcProgressionSolver few(m_baseRules + land.rules(), {Node::dimension(overworld)});
        QVERIFY(few.reached(Node::item(u"qtcraft:obsidian"_s)));
        QVERIFY(!few.reached(lit));
        const QcProgressionGap gap = few.gapTo(lit);
        QVERIFY2(gap.path.contains(u"10 x item qtcraft:obsidian"_s), qPrintable(gap.path.join(u" <- "_s)));

        land.found.insert(obsidian, frameBlocks);
        QVERIFY(QcProgressionSolver(m_baseRules + land.rules(), {Node::dimension(overworld)}).reached(lit));
    }

    // Only what lights a chosen cell lights a frame: fire that burns somewhere does not, lava's sparks
    // and a lighter do (QcServerFire::ignite).
    void burningFireLightsNoPortal()
    {
        const QString lit = Node::portal(u"qtcraft:nether"_s);
        const QSet<QString> start{Node::dimension(overworld), Node::item(u"qtcraft:obsidian"_s)};
        const auto reachedWith = [this, &start, &lit](const QString &node) {
            return QcProgressionSolver(m_baseRules + censusOf(overworld, {node}).rules(), start).reached(lit);
        };

        QVERIFY(!reachedWith(Node::block(u"qtcraft:fire"_s)));
        QVERIFY(!reachedWith(Node::block(u"qtcraft:soul_fire"_s)));
        QVERIFY(reachedWith(Node::block(u"qtcraft:lava"_s)));
        QVERIFY(reachedWith(Node::item(u"qtcraft:flint_and_steel"_s)));
    }

    // The data says water evaporates in the nether, so the graph allows no casting of obsidian there.
    // This is stricter than the game today, where no server code reads the field.
    void waterStaysOnlyWhereItDoesNotEvaporate()
    {
        const QSet<QString> start{Node::item(u"qtcraft:water_bucket"_s), Node::block(u"qtcraft:lava"_s)};
        const QString water = Node::block(u"qtcraft:water"_s);
        QSet<QString> inTheNether = start;
        inTheNether.insert(Node::dimension(nether));
        QVERIFY(!QcProgressionSolver(m_baseRules, inTheNether).reached(water));

        QSet<QString> onLand = start;
        onLand.insert(Node::dimension(overworld));
        const QcProgressionSolver land(m_baseRules, onLand);
        QVERIFY(land.reached(water));
        QVERIFY(land.reached(Node::block(u"qtcraft:obsidian"_s)));
    }

    // A creature barters only for its currency.
    void barterNeedsTheCurrency()
    {
        const QString tuskling = Node::creature(u"qtcraft:tuskling"_s);
        const QString obsidian = Node::item(u"qtcraft:obsidian"_s);
        QVERIFY(!QcProgressionSolver(m_baseRules, {tuskling}).reached(obsidian));
        const QcProgressionSolver paid(m_baseRules, {tuskling, Node::item(u"qtcraft:gold_ingot"_s)});
        QVERIFY(paid.reached(obsidian));
        QCOMPARE(paid.supply(obsidian), QcProgressionSolver::unlimited);
    }

    // An offer of a higher level waits for a trade of the level below.
    void tradesRiseLevelByLevel()
    {
        int checked = 0;
        for (const QcTradeSet &set : m_base->villageCatalog().tradeSets()) {
            for (const QcTradeOffer &offer : set.offers) {
                const QString step = u"trade %1 of %2"_s.arg(offer.id, set.profession);
                const auto rule = std::ranges::find_if(m_baseRules, [&step](const QcProgressionRule &one) { return one.step == step; });
                QVERIFY2(rule != m_baseRules.cend(), qPrintable(step));
                if (offer.level == 2) {
                    QVERIFY2(rule->needs.contains(u"%1 of level 1"_s.arg(set.profession)), qPrintable(step));
                    ++checked;
                }
            }
        }
        QVERIFY(checked > 0);
    }

    // Nodes that only come from each other are named together.
    void aCircleNamesItsNode()
    {
        const QList<QcProgressionRule> rules{{u"a from b"_s, {u"b"_s}, {u"a"_s}}, {u"b from a"_s, {u"a"_s}, {u"b"_s}}};
        const QcProgressionSolver solver(rules, {u"start"_s});
        QVERIFY(!solver.reached(u"a"_s));
        QCOMPARE(solver.gapTo(u"a"_s).missing, QStringList{u"a / b"_s});
    }

    // The nearest gap comes first, with the way to it.
    void aGapShowsTheWay()
    {
        const QList<QcProgressionRule> rules{{u"makes goal"_s, {u"middle"_s, u"start"_s}, {u"goal"_s}}, {u"makes middle"_s, {u"end"_s}, {u"middle"_s}}};
        const QcProgressionGap gap = QcProgressionSolver(rules, {u"start"_s}).gapTo(u"goal"_s);
        QCOMPARE(gap.missing, QStringList{u"end"_s});
        QCOMPARE(gap.path, (QStringList{u"goal"_s, u"makes goal"_s, u"middle"_s, u"makes middle"_s, u"end"_s}));
    }

    // A mod's goal joins the check through its pack, on real worlds, and a later pack replaces a
    // goal by name.
    void aModAddsAndReplacesGoals()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QDir pack(QDir(root.path()).filePath(u"goals"_s));
        QVERIFY(pack.mkpath(u"progression"_s));
        QVERIFY(pack.mkpath(u"items"_s));
        const auto write = [&pack](const QString &path, const QByteArray &text) {
            QFile file(pack.filePath(path));
            return file.open(QIODevice::WriteOnly) && file.write(text) == text.size();
        };
        QVERIFY(write(u"pack.json"_s, R"({"id": "goals", "version": "1.0.0"})"));
        QVERIFY(write(u"items/ruby.json"_s, R"({"name": "ruby"})"));
        QVERIFY(write(u"progression/ruby.json"_s, R"({"name": "ruby", "item": "ruby"})"));
        QVERIFY(write(u"progression/food.json"_s, R"({"name": "qtcraft:food", "item": "qtcraft:stick"})"));
        QVERIFY(write(u"progression/lost_item.json"_s, R"({"name": "lost_item", "item": "no_such_item"})"));
        QVERIFY(write(u"progression/lost_portal.json"_s, R"({"name": "lost_portal", "portal": "no_such_portal"})"));
        QVERIFY(write(u"progression/lost_place.json"_s, R"({"name": "lost_place", "dimension": "no_such_place"})"));

        const QcModLoader::Loaded loaded = QcModLoader::load(root.path());
        QVERIFY(loaded.has_value());
        QCOMPARE(loaded->mods.size(), 1);
        QCOMPARE(loaded->mods.first().state, QcModStatus::State::Loaded);
        const QString warnings = loaded->mods.first().warnings.join(u'\n');
        for (const char *target : {"goals:no_such_item", "goals:no_such_portal", "goals:no_such_place"}) {
            QVERIFY2(warnings.contains(QString::fromLatin1(target)), qPrintable(warnings));
        }
        QCOMPARE(loaded->mods.first().warnings.size(), 3);

        const QcContentRegistry &content = *loaded->content;
        const WorldSample &sample = m_samples.first();
        const QList<QcProgressionResult> results = QcProgressionRules::check(content, QcProgressionRules::ofContent(content), sample.censuses, overworld);
        const QcProgressionResult *ruby = resultFor(results, u"goals:ruby"_s);
        QVERIFY(ruby);
        QVERIFY(!ruby->met);
        QCOMPARE(ruby->missing, QStringList{Node::item(u"goals:ruby"_s)});
        const QList<QcProgressionGoal> &goals = content.worldgen().progressionGoals();
        const auto food = std::ranges::find_if(goals, [](const QcProgressionGoal &goal) { return goal.name == u"qtcraft:food"_s; });
        QVERIFY(food != goals.cend());
        QCOMPARE(food->kind, QcProgressionGoal::Kind::Item);
    }

    void goalFiles_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<QString>("error");
        QTest::newRow("item") << QByteArray(R"({"name": "a", "item": "b", "count": 3})") << QString();
        QTest::newRow("portal") << QByteArray(R"({"name": "a", "portal": "b"})") << QString();
        QTest::newRow("food") << QByteArray(R"({"name": "a", "food": true})") << QString();
        QTest::newRow("no name") << QByteArray(R"({"item": "b"})") << u"'name' is required"_s;
        QTest::newRow("empty target") << QByteArray(R"({"name": "a", "item": ""})") << u"must name something"_s;
        QTest::newRow("no kind") << QByteArray(R"({"name": "a"})") << u"exactly one"_s;
        QTest::newRow("two kinds") << QByteArray(R"({"name": "a", "item": "b", "fire": true})") << u"exactly one"_s;
        QTest::newRow("fire false") << QByteArray(R"({"name": "a", "fire": false})") << u"can only be true"_s;
        QTest::newRow("count of a portal") << QByteArray(R"({"name": "a", "portal": "b", "count": 2})") << u"goes only with"_s;
        QTest::newRow("count too high") << QByteArray(R"({"name": "a", "item": "b", "count": 10000})") << u"count"_s;
    }

    void goalFiles()
    {
        QFETCH(QByteArray, json);
        QFETCH(QString, error);
        const QcResult<QcProgressionGoal> goal = QcContentParser::parseProgressionGoal(QJsonDocument::fromJson(json).object(), u"pack"_s);
        if (error.isEmpty()) {
            QVERIFY2(goal.has_value(), goal ? "" : qPrintable(goal.error()));
            QCOMPARE(goal->name, u"pack:a"_s);
        } else {
            QVERIFY(!goal.has_value());
            QVERIFY2(goal.error().contains(error), qPrintable(goal.error()));
        }
    }
};

QTEST_GUILESS_MAIN(TestProgressionReachability)
#include "test_progression_reachability.moc"
