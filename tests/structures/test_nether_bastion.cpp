// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/worldgen/QcNetherParser.h"
#include "fixtures/QcChunkSite.h"
#include "fixtures/QcTestUtil.h"
#include "worldgen/bastion/QcBastionGenerator.h"
#include "worldgen/generators/QcRegionPlans.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worldgen/nether/QcNetherGenerator.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QTest>

#include <array>
#include <memory>
#include <thread>
#include <vector>

using namespace Qt::StringLiterals;

// The bastion ruins of the nether (decisions E-1087 to E-1089): real generated chunks of natural
// seeds. Seeds as in decision E-846: fixed ones plus a random one per run, in the log; QC_TEST_SEED
// sets it.
namespace {

using Plan = QcBastionPlan;
using PlanPointer = std::shared_ptr<const QcBastionPlan>;
using Form = QcBastionPlan::Form;
using Paint = QcBastionPlan::Paint;

constexpr std::array<qint64, 3> fixedSeeds{42, 1, 20261106};
// The regions around the origin searched per side.
constexpr int searchRegions = 10;

} // namespace

class TestNetherBastion : public QObject
{
    Q_OBJECT

private:
    // A generated bastion: chunks are dear in Debug, so the tests that read blocks share these.
    struct Sample
    {
        qint64 seed = 0;
        std::shared_ptr<QcNetherGenerator> generator;
        PlanPointer plan;
        std::shared_ptr<const QcChunkSite> site;
    };

    [[nodiscard]] static QString residentName(const QcNetherGenerator &generator, Plan::Folk folk)
    {
        const QcNetherSettings::Bastion &settings = generator.definitions().settings.bastion;
        switch (folk) {
        case Plan::Folk::Bruiser:
            return settings.bruiser;
        case Plan::Folk::Beast:
            return settings.beast;
        case Plan::Folk::Guard:
            break;
        }
        return settings.guard;
    }

    // Chunks of the plan without a resident say so, and so does one far from any bastion.
    static void checkChunksWithoutResidents(const Sample &sample)
    {
        QSet<QcChunkPos> peopled;
        for (const Plan::Resident &resident : sample.plan->residents) {
            peopled.insert(resident.at.chunk());
        }
        int empty = 0;
        const QcChunkPos low = QcBlockPos{sample.plan->bounds.minX, 0, sample.plan->bounds.minZ}.chunk();
        const QcChunkPos high = QcBlockPos{sample.plan->bounds.maxX, 0, sample.plan->bounds.maxZ}.chunk();
        for (int z = low.z; z <= high.z; ++z) {
            for (int x = low.x; x <= high.x; ++x) {
                if (!peopled.contains({x, z})) {
                    ++empty;
                    QVERIFY(!sample.generator->hasResidentsIn({x, z}));
                    QVERIFY(sample.generator->furnishingIn({x, z}).residents.isEmpty());
                }
            }
        }
        QVERIFY(empty > 0);
    }

    // The lava cells of `box` and the first of them that runs out.
    struct Basin
    {
        int cells = 0;
        QString leak;
    };

    [[nodiscard]] Basin basinOf(const QcChunkSite &site, const QcBlockBox &box) const
    {
        Basin basin;
        for (int y = box.low.y; y <= box.high.y; ++y) {
            for (int z = box.low.z; z <= box.high.z; ++z) {
                for (int x = box.low.x; x <= box.high.x; ++x) {
                    if (!isLava(site.at({x, y, z}))) {
                        continue;
                    }
                    ++basin.cells;
                    for (const QcBlockPos &next : {QcBlockPos{x + 1, y, z}, {x - 1, y, z}, {x, y, z + 1}, {x, y, z - 1}, {x, y - 1, z}}) {
                        if (passable(site.at(next))) {
                            basin.leak = u"lava at %1 runs to %2"_s.arg(cellText({x, y, z}), cellText(next));
                        }
                    }
                }
            }
        }
        return basin;
    }

    // Every lava cell of the chunk at `pos` that lies in a part of `plan` is a cell it painted.
    static void checkNoPool(const QcNetherGenerator &generator, const QcBastionPlan &plan, QcChunkPos pos)
    {
        const auto chunk = generator.generate(pos);
        const QList<QcNetherDefinitions::Pool> &pools = generator.definitions().pools;
        const auto own = [&plan](QcBlockPos cell) {
            return std::ranges::any_of(plan.strokes, [cell](const Plan::Stroke &stroke) { return stroke.paint == Paint::Lava && stroke.box.contains(cell); });
        };
        for (int i = 0; i < QcWorld::chunkSize * QcWorld::chunkSize; ++i) {
            const QcBlockPos column{pos.x * QcWorld::chunkSize + i % QcWorld::chunkSize, 0, pos.z * QcWorld::chunkSize + i / QcWorld::chunkSize};
            for (int y = 33; y < QcWorld::worldHeight; ++y) {
                const QcBlockPos cell{column.x, y, column.z};
                const QcBlockId block = chunk->block(i % QcWorld::chunkSize, y, i / QcWorld::chunkSize);
                const bool pool = std::ranges::any_of(pools, [block](const QcNetherDefinitions::Pool &each) { return each.block == block; });
                const bool inPart = std::ranges::any_of(plan.parts, [cell](const QcBlockBox &part) { return part.contains(cell); });
                QVERIFY2(!pool || !inPart || own(cell), qPrintable(u"lava at %1 %2 %3"_s.arg(cell.x).arg(cell.y).arg(cell.z)));
            }
        }
    }

    [[nodiscard]] static QList<qint64> seeds()
    {
        QList<qint64> all(fixedSeeds.begin(), fixedSeeds.end());
        all.append(QcTestUtil::randomSeed());
        return all;
    }

    [[nodiscard]] std::shared_ptr<QcNetherGenerator> generatorOf(qint64 seed) const { return std::make_shared<QcNetherGenerator>(m_content, seed); }

    [[nodiscard]] static QList<PlanPointer> plansOf(const QcBastionGenerator &generator)
    {
        QList<PlanPointer> plans;
        for (int z = -searchRegions / 2; z < searchRegions / 2; ++z) {
            for (int x = -searchRegions / 2; x < searchRegions / 2; ++x) {
                if (PlanPointer plan = generator.planOf(x, z)) {
                    plans.append(std::move(plan));
                }
            }
        }
        return plans;
    }

    [[nodiscard]] static QList<PlanPointer> plansOf(const QcNetherGenerator &generator) { return plansOf(generator.bastion()); }

    // The nether of a showcase world of `seed`, with the arrival station and its bastions.
    [[nodiscard]] std::shared_ptr<QcNetherGenerator> showcaseOf(qint64 seed) const
    {
        const std::shared_ptr<QcWorldGenerator> created = QcWorldGenerators::create(QString::fromLatin1(QcWorldGenerators::nether), m_content, seed,
                                                                                    QString::fromLatin1(QcWorldGenerators::netherStation), 1);
        return std::dynamic_pointer_cast<QcNetherGenerator>(created);
    }

    // One natural bastion per form of a fixed and a random seed, turned as they fall, and the forms
    // of a showcase.
    [[nodiscard]] QList<Sample> pickSamples() const
    {
        QList<Sample> picked;
        const qint64 seed = QcTestUtil::randomSeed();
        for (const qint64 each : {fixedSeeds[0], seed}) {
            const std::shared_ptr<QcNetherGenerator> natural = generatorOf(each);
            QSet<Form> forms;
            const QList<PlanPointer> plans = plansOf(*natural);
            for (const PlanPointer &plan : plans) {
                if (!forms.contains(plan->form)) {
                    forms.insert(plan->form);
                    picked.append({each, natural, plan, std::make_shared<const QcChunkSite>(*natural, plan->bounds)});
                }
            }
        }
        const std::shared_ptr<QcNetherGenerator> showcase = showcaseOf(seed);
        for (const PlanPointer &plan : showcase->bastion().demonstrations()) {
            picked.append({seed, showcase, plan, std::make_shared<const QcChunkSite>(*showcase, plan->bounds)});
        }
        return picked;
    }

    [[nodiscard]] const QList<Sample> &samples()
    {
        if (m_samples.isEmpty()) {
            m_samples = pickSamples();
            qInfo() << "bastions generated:" << m_samples.size();
        }
        return m_samples;
    }

    [[nodiscard]] bool standable(QcBlockId id) const { return m_content->block(id).solid && !m_content->block(id).liquid; }
    [[nodiscard]] bool passable(QcBlockId id) const { return !m_content->block(id).solid && !m_content->block(id).liquid; }
    [[nodiscard]] bool isLava(QcBlockId id) const { return m_content->block(id).liquid; }
    [[nodiscard]] QString blockName(QcBlockId id) const { return m_content->block(id).name; }
    [[nodiscard]] static QString cellText(QcBlockPos cell) { return u"%1 %2 %3"_s.arg(cell.x).arg(cell.y).arg(cell.z); }

    [[nodiscard]] QSet<quint64> walk(const Sample &sample) const
    {
        return qcSiteWalk(*sample.site, sample.plan->entrance, sample.plan->bounds, [this](QcBlockId id) { return passable(id); }, [this](QcBlockId id) { return standable(id); });
    }

    // Settings with a fortress and a bastion whose keys are `changes` over names that are all there.
    [[nodiscard]] static QcResult<QcNetherSettings> parse(const QJsonObject &changes, const QStringList &without = {})
    {
        QJsonObject fortress{{u"monsters"_s, QJsonArray{QJsonObject{{u"mob"_s, u"skeleton"_s}, {u"weight"_s, 2}}}}, {u"chance"_s, 0.4}};
        for (const char *key : {"brick", "cracked", "chiseled", "lane", "fence", "stairs", "light", "soil", "crop", "chest", "spawner", "spawnerMob", "lootTable"}) {
            fortress.insert(QLatin1StringView(key), u"stone"_s);
        }
        QJsonObject bastion{{u"biomes"_s, QJsonArray{u"nether_wastes"_s}}};
        for (const char *key : {"rough", "brick", "cracked", "polished", "chiseled", "gilded", "gold", "soil", "lava", "wall", "stairs", "lantern", "chest", "spawner", "spawnerMob", "guard", "bruiser", "beast", "bridgeLoot", "stablesLoot", "treasureLoot", "otherLoot"}) {
            bastion.insert(QLatin1StringView(key), u"stone"_s);
        }
        for (auto it = changes.begin(); it != changes.end(); ++it) {
            bastion.insert(it.key(), it.value());
        }
        for (const QString &key : without) {
            bastion.remove(key);
        }
        return QcNetherParser::parse(QJsonObject{{u"fortress"_s, fortress}, {u"bastion"_s, bastion}}, u"qtcraft"_s);
    }

private slots:
    void initTestCase()
    {
        m_content = QcTestUtil::loadContent();
        QVERIFY(m_content);
    }

    // The same seed plans the same bastions in any order of the regions; another seed others.
    void plansFollowTheSeedAndNothingElse()
    {
        const QList<qint64> all = seeds();
        for (const qint64 seed : all) {
            const std::shared_ptr<QcNetherGenerator> first = generatorOf(seed);
            const std::shared_ptr<QcNetherGenerator> second = generatorOf(seed);
            QList<PlanPointer> plans = plansOf(*first);
            std::ranges::reverse(plans);
            QVERIFY(!plans.isEmpty());
            for (const PlanPointer &plan : std::as_const(plans)) {
                const PlanPointer again = second->bastion().planOf(plan->regionX, plan->regionZ);
                QVERIFY(again);
                QCOMPARE(again->centre, plan->centre);
                QCOMPARE(again->form, plan->form);
                QCOMPARE(again->strokes.size(), plan->strokes.size());
                QCOMPARE(again->bounds, plan->bounds);
            }
        }
        const QList<PlanPointer> one = plansOf(*generatorOf(1));
        const QList<PlanPointer> other = plansOf(*generatorOf(2));
        QVERIFY(one.constFirst()->centre != other.constFirst()->centre);
    }

    void aChunkComesOutTheSameInAnyOrder()
    {
        const Sample &sample = samples().constFirst();
        const QcBlockPos middle = sample.plan->centre;
        const std::shared_ptr<QcNetherGenerator> fresh = generatorOf(sample.seed);
        for (int i = 8; i >= 0; --i) {
            const QcChunkPos pos{middle.chunk().x - 1 + i % 3, middle.chunk().z - 1 + i / 3};
            const std::shared_ptr<QcChunk> chunk = fresh->generate(pos);
            for (int x = 0; x < QcWorld::chunkSize; ++x) {
                for (int z = 0; z < QcWorld::chunkSize; ++z) {
                    for (int y = 20; y < 70; ++y) {
                        QCOMPARE(chunk->block(x, y, z), sample.site->at({pos.x * QcWorld::chunkSize + x, y, pos.z * QcWorld::chunkSize + z}));
                    }
                }
            }
        }
    }

    void chunksComeOutTheSameOnFourThreads()
    {
        const Sample &sample = samples().constFirst();
        const std::shared_ptr<QcNetherGenerator> shared = generatorOf(sample.seed);
        const QcChunkPos middle = sample.plan->centre.chunk();
        std::array<std::shared_ptr<QcChunk>, 4> chunks;
        std::vector<std::thread> threads;
        threads.reserve(chunks.size());
        for (std::shared_ptr<QcChunk> &chunk : chunks) {
            threads.emplace_back([&chunk, &shared, middle]() { chunk = shared->generate(middle); });
        }
        for (std::thread &thread : threads) {
            thread.join();
        }
        for (const std::shared_ptr<QcChunk> &chunk : chunks) {
            for (int i = 0; i < QcWorld::chunkSize * QcWorld::chunkSize; ++i) {
                QCOMPARE(chunk->block(i % QcWorld::chunkSize, 40, i / QcWorld::chunkSize), chunks[0]->block(i % QcWorld::chunkSize, 40, i / QcWorld::chunkSize));
            }
        }
    }

    // A region holds one of the two at most, three bastions to two fortresses, and the four forms
    // fall alike (decision E-1088).
    void theRegionsShareTheRoleModelsWeights()
    {
        int bastions = 0;
        int fortresses = 0;
        std::array<int, Plan::formCount> forms{};
        for (const qint64 seed : fixedSeeds) {
            const std::shared_ptr<QcNetherGenerator> world = generatorOf(seed);
            for (int i = 0; i < 40 * 40; ++i) {
                const int x = i % 40 - 20;
                const int z = i / 40 - 20;
                const QcNetherComplexes::Kind kind = world->complexes().kindOf(x, z);
                bastions += kind == QcNetherComplexes::Kind::Bastion ? 1 : 0;
                fortresses += kind == QcNetherComplexes::Kind::Fortress ? 1 : 0;
                QVERIFY(kind != QcNetherComplexes::Kind::None);
            }
            const QList<PlanPointer> plans = plansOf(*world);
            for (const PlanPointer &plan : plans) {
                ++forms[static_cast<std::size_t>(plan->form)];
                QVERIFY(world->fortress().planOf(plan->regionX, plan->regionZ) == nullptr);
            }
        }
        // Basalt deltas turn bastions into fortresses: the share lies under the role model's 0.6.
        const double share = static_cast<double>(bastions) / (bastions + fortresses);
        qInfo() << "share of bastions" << share << "forms" << forms[0] << forms[1] << forms[2] << forms[3];
        QVERIFY2(share > 0.4 && share <= 0.62, qPrintable(QString::number(share)));
        QVERIFY(std::ranges::all_of(forms, [](int count) { return count > 0; }));
    }

    void aBastionStandsOnlyInItsBiomes()
    {
        const QList<qint64> all = seeds();
        for (const qint64 seed : all) {
            const std::shared_ptr<QcNetherGenerator> world = generatorOf(seed);
            const QList<PlanPointer> plans = plansOf(*world);
            for (const PlanPointer &plan : plans) {
                const QString biome = world->biomeAt(plan->centre.x, plan->centre.z);
                QVERIFY2(biome != u"qtcraft:basalt_deltas"_s, qPrintable(cellText(plan->centre)));
                QVERIFY(world->definitions().settings.bastion.biomes.contains(biome));
            }
        }
    }

    // Everything of a plan lies inside its region, so no chunk cuts it off at a region's edge.
    void aBastionStaysInItsRegion()
    {
        const QList<qint64> all = seeds();
        for (const qint64 seed : all) {
            const std::shared_ptr<QcNetherGenerator> world = generatorOf(seed);
            const int size = world->definitions().settings.bastion.regionSize;
            const QList<PlanPointer> plans = plansOf(*world);
            for (const PlanPointer &plan : plans) {
                QCOMPARE(plan->centre.y, world->definitions().settings.bastion.floor);
                const QcXzRect region{plan->regionX * size, plan->regionZ * size, (plan->regionX + 1) * size - 1, (plan->regionZ + 1) * size - 1};
                QVERIFY(region.contains(plan->bounds.minX, plan->bounds.minZ));
                QVERIFY(region.contains(plan->bounds.maxX, plan->bounds.maxZ));
                QVERIFY(plan->bounds.maxX - plan->centre.x <= Plan::reach + 2);
                QVERIFY(plan->centre.x - plan->bounds.minX <= Plan::reach + 2);
                for (const Plan::Stroke &stroke : plan->strokes) {
                    QVERIFY2(plan->bounds.grown(-2).contains(stroke.box.low.x, stroke.box.low.z), qPrintable(cellText(stroke.box.low)));
                    QVERIFY2(plan->bounds.grown(-2).contains(stroke.box.high.x, stroke.box.high.z), qPrintable(cellText(stroke.box.high)));
                }
            }
        }
    }

    // The columns of a foundation end on what carries them, and its top is no hanging cell: a wall
    // stands on rock, through the lava if need be.
    void foundationsReachTheRock()
    {
        for (const Sample &sample : samples()) {
            const QcChunkSite &site = *sample.site;
            for (const Plan::Stroke &stroke : sample.plan->strokes) {
                if (stroke.paint != Paint::Foundation) {
                    continue;
                }
                for (int i = 0; i < 4; ++i) {
                    const QcBlockPos corner{i % 2 == 0 ? stroke.box.low.x : stroke.box.high.x, stroke.box.high.y, i / 2 == 0 ? stroke.box.low.z : stroke.box.high.z};
                    QcBlockPos cell = corner;
                    while (cell.y > 1 && blockName(site.at(cell)) == u"qtcraft:blackstone"_s) {
                        cell = cell.offset(0, -1, 0);
                    }
                    QVERIFY2(standable(site.at(cell)), qPrintable(u"foundation at %1 hangs over %2 at %3"_s.arg(cellText(corner), blockName(site.at(cell))).arg(cell.y)));
                }
            }
        }
    }

    // From the landing every chest and every resident is reached on foot, also those on a pillar
    // over the lava.
    void aBastionCanBeWalkedThrough()
    {
        for (const Sample &sample : samples()) {
            const QSet<quint64> reached = walk(sample);
            const Plan &plan = *sample.plan;
            qInfo() << "form" << static_cast<int>(plan.form) << "cells" << reached.size();
            for (const Plan::Chest &chest : plan.chests) {
                // A cell around the chest.
                bool beside = false;
                for (const auto &[dx, dz] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
                    for (const int dy : {0, -1, 1}) {
                        beside = beside || reached.contains(qcSiteCellKey(chest.at.offset(dx, dy, dz)));
                    }
                }
                QVERIFY2(beside, qPrintable(u"seed %1 form %2: chest at %3 not reached"_s.arg(sample.seed).arg(static_cast<int>(plan.form)).arg(cellText(chest.at))));
            }
            for (const Plan::Resident &resident : plan.residents) {
                const bool penned = resident.folk == Plan::Folk::Beast;
                QVERIFY2(penned || reached.contains(qcSiteCellKey(resident.at)), qPrintable(u"seed %1 form %2: resident at %3 not reached"_s.arg(sample.seed).arg(static_cast<int>(plan.form)).arg(cellText(resident.at))));
            }
        }
    }

    void theResidentsHaveRoom()
    {
        for (const Sample &sample : samples()) {
            const QcChunkSite &site = *sample.site;
            for (const Plan::Resident &resident : sample.plan->residents) {
                const QString where = u"form %1 at %2"_s.arg(static_cast<int>(sample.plan->form)).arg(cellText(resident.at));
                QVERIFY2(passable(site.at(resident.at)), qPrintable(where + u" in "_s + blockName(site.at(resident.at))));
                QVERIFY2(passable(site.at(resident.at.offset(0, 1, 0))), qPrintable(where));
                QVERIFY2(standable(site.at(resident.at.offset(0, -1, 0))), qPrintable(where + u" over "_s + blockName(site.at(resident.at.offset(0, -1, 0)))));
            }
        }
    }

    // Every form has what the role model's has: chests with their tables, the treasure hall its
    // spawners and bruisers everywhere; beasts only in the stables.
    void everyFormHasItsParts()
    {
        for (const Sample &sample : samples()) {
            const Plan &plan = *sample.plan;
            QVERIFY(!plan.chests.isEmpty());
            QVERIFY(!plan.parts.isEmpty());
            const auto count = [&plan](Plan::Folk folk) { return std::ranges::count(plan.residents, folk, &Plan::Resident::folk); };
            const auto holds = [&plan](Plan::Loot loot) { return std::ranges::any_of(plan.chests, [loot](Plan::Chest chest) { return chest.loot == loot; }); };
            QVERIFY(count(Plan::Folk::Bruiser) >= 2);
            QVERIFY(count(Plan::Folk::Guard) >= 3);
            QCOMPARE(count(Plan::Folk::Beast) > 0, plan.form == Form::Stables);
            QCOMPARE(plan.spawners.isEmpty(), plan.form != Form::Treasure);
            QVERIFY(holds(Plan::Loot::Treasure) == (plan.form == Form::Treasure));
            QVERIFY(holds(Plan::Loot::Bridge) == (plan.form == Form::Bridge));
            QVERIFY(holds(Plan::Loot::Stables) == (plan.form == Form::Stables));
        }
    }

    // A chest and a spawner stand where the plan says, with the chest's table and the kind.
    void theFurnishingFollowsThePlan()
    {
        for (const Sample &sample : samples()) {
            const Plan &plan = *sample.plan;
            for (const Plan::Chest &chest : plan.chests) {
                QVERIFY2(blockName(sample.site->at(chest.at)).endsWith(u"chest"_s), qPrintable(blockName(sample.site->at(chest.at))));
                const QcChunkFurnishing furnishing = sample.generator->furnishingIn(chest.at.chunk());
                QVERIFY(std::ranges::any_of(furnishing.chests, [&chest](const QcChunkFurnishing::Chest &found) { return found.at == chest.at && found.lootTable.contains(u"bastion_"_s); }));
            }
            for (const QcBlockPos &spawner : plan.spawners) {
                QCOMPARE(blockName(sample.site->at(spawner)), u"qtcraft:spawner"_s);
                const QcChunkFurnishing furnishing = sample.generator->furnishingIn(spawner.chunk());
                QVERIFY(std::ranges::any_of(furnishing.spawners, [&spawner](const auto &found) { return found.at == spawner && found.mob == u"qtcraft:magma_slime"_s; }));
            }
            for (const Plan::Resident &resident : plan.residents) {
                QVERIFY(sample.generator->hasResidentsIn(resident.at.chunk()));
                const QString name = residentName(*sample.generator, resident.folk);
                const QcChunkFurnishing furnishing = sample.generator->furnishingIn(resident.at.chunk());
                QVERIFY(std::ranges::any_of(furnishing.residents, [&resident, &name](const QcChunkFurnishing::Resident &found) { return found.at == resident.at && found.mob == name; }));
            }
            checkChunksWithoutResidents(sample);
        }
    }

    // The lava of a bastion lies in basins: every cell has lava or a solid block on its sides and
    // below, so nothing starts to flow; the bridges over it are whole.
    void theLavaStaysInItsBasins()
    {
        for (const Sample &sample : samples()) {
            const QcChunkSite &site = *sample.site;
            int cells = 0;
            for (const Plan::Stroke &stroke : sample.plan->strokes) {
                if (stroke.paint != Paint::Lava) {
                    continue;
                }
                const Basin basin = basinOf(site, stroke.box);
                QVERIFY2(basin.leak.isEmpty(), qPrintable(u"form %1: %2"_s.arg(static_cast<int>(sample.plan->form)).arg(basin.leak)));
                cells += basin.cells;
            }
            QVERIFY2(sample.plan->form == Form::Treasure ? cells > 1000 : cells >= 24, qPrintable(QString::number(cells)));
        }
    }

    // A lantern of the plan stands on its post: the crumbling of a deck takes no post away.
    void noLanternFloats()
    {
        for (const Sample &sample : samples()) {
            for (const Plan::Stroke &stroke : sample.plan->strokes) {
                if (stroke.paint != Paint::Lantern) {
                    continue;
                }
                const QcBlockPos at = stroke.box.low;
                const QString below = blockName(sample.site->at(at.offset(0, -1, 0)));
                QVERIFY2(below != u"qtcraft:air"_s, qPrintable(u"seed %1: lantern at %2"_s.arg(sample.seed).arg(cellText(at))));
            }
        }
    }

    // No pool of the terrain lies in a part (decision E-1072): lava there is the bastion's own. The
    // pools lie in the basalt deltas, which a bastion reaches only with its edge.
    void noPoolLiesInAPart()
    {
        const Sample &sample = samples().constFirst();
        QSet<QcChunkPos> checked;
        for (int region = 0; region < 144 && checked.size() < 12; ++region) {
            const PlanPointer plan = sample.generator->bastion().planOf(region % 12 - 6, region / 12 - 6);
            if (!plan) {
                continue;
            }
            for (const QcBlockBox &part : plan->parts) {
                for (int z = part.low.z; z <= part.high.z; z += QcWorld::chunkSize) {
                    for (int x = part.low.x; x <= part.high.x; x += QcWorld::chunkSize) {
                        const QcChunkPos pos = QcBlockPos{x, 0, z}.chunk();
                        if (sample.generator->biomeAt(x, z) == u"qtcraft:basalt_deltas"_s && !checked.contains(pos)) {
                            checked.insert(pos);
                            checkNoPool(*sample.generator, *plan, pos);
                            if (QTest::currentTestFailed()) {
                                return;
                            }
                        }
                    }
                }
            }
        }
        QVERIFY(!checked.isEmpty());
    }

    // The loot of the role model's tables; what the game does not have yet waits in marked slots
    // that keep its weight: a slot whose item came must turn into an entry.
    void theLootWaitsForWhatIsMissing()
    {
        QSet<QString> pending;
        for (const QString &name : {u"bastion_bridge"_s, u"bastion_stables"_s, u"bastion_treasure"_s, u"bastion_other"_s}) {
            QFile file(u":/content/qtcraft/loot/"_s + name + u".json"_s);
            QVERIFY(file.open(QIODevice::ReadOnly));
            const QJsonArray pools = QJsonDocument::fromJson(file.readAll()).object().value(u"pools"_s).toArray();
            QVERIFY(m_content->lootTables().find(u"qtcraft:"_s + name));
            for (const auto &pool : pools) {
                const QJsonArray entries = pool.toObject().value(u"entries"_s).toArray();
                for (const auto &entry : entries) {
                    const QString item = entry.toObject().value(u"item"_s).toString();
                    const QString waiting = entry.toObject().value(u"pending"_s).toString();
                    QVERIFY2(item.isEmpty() || m_content->itemId(u"qtcraft:"_s + item), qPrintable(item));
                    QVERIFY2(waiting.isEmpty() || !m_content->itemId(u"qtcraft:"_s + waiting), qPrintable(waiting + u" exists now"_s));
                    if (!waiting.isEmpty()) {
                        pending.insert(waiting);
                    }
                }
            }
        }
        // Netherite came with E-1119: its slots are entries now.
        QVERIFY(!pending.contains(u"netherite_scrap"_s));
        QVERIFY(!pending.contains(u"netherite_upgrade_smithing_template"_s));
        QVERIFY(!pending.contains(u"ancient_debris"_s));
    }

    void locateFindsTheNearest()
    {
        const std::shared_ptr<QcNetherGenerator> world = generatorOf(fixedSeeds[0]);
        QVERIFY(world->locatableNames().contains(u"bastion"_s));
        const QcLocateResult found = world->locate(u"bastion", {0, 64, 0}, 4000);
        QVERIFY(found.place.has_value());
        const QList<PlanPointer> plans = plansOf(*world);
        const auto distance = [](const PlanPointer &plan) { return QcPlaceSearch::distance(plan->middle(), {0, 0, 0}); };
        const PlanPointer nearest = *std::ranges::min_element(plans, {}, distance);
        QCOMPARE(found.place->at, nearest->middle());
    }

    // Without kinds of its own a bastion brings forth the biome's monsters, as in the role model;
    // a mod's kinds come in the boxes of its parts (decision E-1089).
    void theSpawnAreaIsAMatterOfTheData()
    {
        const std::shared_ptr<QcNetherGenerator> world = generatorOf(fixedSeeds[0]);
        const PlanPointer plan = plansOf(*world).constFirst();
        QVERIFY(world->bastion().sitesNear(plan->centre, 64).spawnAreas.isEmpty());

        // A generator with monsters in the data: an area of its plan's parts with those kinds.
        QcNetherDefinitions definitions = world->definitions();
        QcStructureDefinition::SpawnArea::Monster monster;
        monster.mob = u"qtcraft:tuskling"_s;
        monster.weight = 3;
        definitions.settings.bastion.monsters = {monster};
        const QcBastionGenerator bastion(definitions, world->terrain(), world->complexes(), *m_content, fixedSeeds[0]);
        const PlanPointer own = plansOf(bastion).constFirst();
        const QcStructureSites sites = bastion.sitesNear(own->centre, 64);
        QCOMPARE(sites.spawnAreas.size(), 1);
        const QcSpawnArea &area = sites.spawnAreas.constFirst();
        QCOMPARE(area.monsters, definitions.settings.bastion.monsters);
        QCOMPARE(area.parts, own->parts);
        const QcBlockBox &part = own->parts.constFirst();
        QVERIFY(area.contains(part.low));
        QVERIFY(area.contains(part.high));
        QVERIFY(!area.contains(part.low.offset(0, -1000, 0)));
    }

    // The sites of a region not planned yet are unknown, not empty; the planned ones are the same
    // as those planning gives.
    void plannedSitesWaitForThePlans()
    {
        const std::shared_ptr<QcNetherGenerator> world = generatorOf(fixedSeeds[0]);
        const QcBlockPos at = plansOf(*generatorOf(fixedSeeds[0])).constFirst()->centre;
        QVERIFY(!world->plannedStructureSitesNear(at, 64));
        const QcStructureSites sites = world->structureSitesNear(at, 64);
        const std::optional<QcStructureSites> planned = world->plannedStructureSitesNear(at, 64);
        QVERIFY(planned);
        QCOMPARE(planned->spawnAreas.size(), sites.spawnAreas.size());
    }

    // A full cache starts over: nothing is planned twice before that, a region it forgot is
    // unknown again and comes out the same.
    void theCacheForgetsAtItsLimit()
    {
        struct Dot
        {
            QcXzRect bounds;
            [[nodiscard]] bool touches(QcXzRect area) const { return bounds.intersects(area); }
            [[nodiscard]] QcBlockPos middle() const { return {bounds.minX, 0, bounds.minZ}; }
        };
        constexpr int size = 16;
        int planned = 0;
        const QcRegionPlans<Dot> plans(size, [&planned](int regionX, int regionZ) {
            ++planned;
            return std::make_shared<const Dot>(Dot{{regionX * size, regionZ * size, regionX * size, regionZ * size}});
        });
        const QcXzRect first{0, 0, 0, 0};
        const std::shared_ptr<const Dot> dot = plans.planOf(0, 0);
        QVERIFY(plans.plannedIn(first));
        for (int region = 1; region < 256; ++region) {
            QVERIFY(plans.planOf(region, 0));
        }
        QCOMPARE(plans.planOf(0, 0), dot);
        QCOMPARE(planned, 256);

        QVERIFY(plans.planOf(256, 0));
        QVERIFY(!plans.plannedIn(first));
        QCOMPARE(plans.planOf(0, 0)->bounds, dot->bounds);
        QCOMPARE(planned, 258);
    }

    void theParserKeepsABastionInItsRegion()
    {
        const QcResult<QcNetherSettings> plain = parse({});
        QVERIFY2(plain, qPrintable(plain ? QString() : plain.error()));
        QCOMPARE(plain->bastion.regionSize, 448);
        QCOMPARE(plain->bastion.chance, 0.6);
        QCOMPARE(plain->bastion.floor, 33);
        QCOMPARE(plain->bastion.biomes, QStringList{u"qtcraft:nether_wastes"_s});
        QVERIFY(!parse({{u"regionSize"_s, 512}}));
        QVERIFY(!parse({{u"chance"_s, 0.7}}));
        QVERIFY(!parse({}, {u"brick"_s}));
        QVERIFY(!parse({}, {u"biomes"_s}));
    }

    // The showcase shows all four forms, named, west of the station, and natural ones keep away.
    void theShowcaseHasEveryForm()
    {
        const std::shared_ptr<QcNetherGenerator> showcase = showcaseOf(1);
        QVERIFY(showcase);
        const QList<PlanPointer> &plans = showcase->bastion().demonstrations();
        QCOMPARE(plans.size(), Plan::formCount);
        const QcBlockPos arrival = showcase->spawnPoint();
        QSet<QString> texts;
        for (const PlanPointer &plan : plans) {
            QVERIFY(!plan->bounds.intersects(QcXzRect{arrival.x, arrival.z, arrival.x, arrival.z}.grown(24)));
            QVERIFY(plan->bounds.maxX < arrival.x);
            QVERIFY(showcase->fortress().plansIn(plan->bounds).isEmpty());
            for (const PlanPointer &other : plans) {
                QVERIFY(plan == other || !plan->bounds.intersects(other->bounds));
            }
            for (int z = plan->bounds.minZ >> 4; z <= plan->bounds.maxZ >> 4; ++z) {
                for (int x = plan->bounds.minX >> 4; x <= plan->bounds.maxX >> 4; ++x) {
                    const QList<QcWorldLabel> labels = showcase->labelsIn({x, z});
                    for (const QcWorldLabel &label : labels) {
                        texts.insert(label.text);
                    }
                }
            }
        }
        for (const QString &text : {u"structure.qtcraft.bastion_bridge"_s, u"structure.qtcraft.bastion_stables"_s, u"structure.qtcraft.bastion_treasure"_s, u"structure.qtcraft.bastion_housing"_s}) {
            QVERIFY2(texts.contains(text), qPrintable(text));
        }
    }

    void ruinsKeepOutOfABastion()
    {
        for (const qint64 seed : fixedSeeds) {
            const std::shared_ptr<QcNetherGenerator> world = generatorOf(seed);
            const QList<QcNetherDefinitions::Structure> &kinds = world->definitions().structures;
            const QList<PlanPointer> plans = plansOf(*world);
            for (const PlanPointer &plan : plans) {
                for (const QcNetherDefinitions::Structure &kind : kinds) {
                    for (int i = 0; i < 9; ++i) {
                        const int x = qcFloorDiv(plan->centre.x, kind.spacing) - 1 + i % 3;
                        const int z = qcFloorDiv(plan->centre.z, kind.spacing) - 1 + i / 3;
                        if (const std::optional<QcStructurePlacement> ruin = world->structures().planOf(kind, x, z)) {
                            const QcXzRect footprint{ruin->origin().x, ruin->origin().z, ruin->origin().x + ruin->sizeX() - 1, ruin->origin().z + ruin->sizeZ() - 1};
                            QVERIFY(!plan->touches(footprint));
                        }
                    }
                }
            }
        }
    }

private:
    std::shared_ptr<const QcContentRegistry> m_content;
    QList<Sample> m_samples;
};

QTEST_GUILESS_MAIN(TestNetherBastion)
#include "test_nether_bastion.moc"
