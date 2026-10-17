#include "common/content/loader/QcBaseContent.h"
#include "common/world/QcDayCycle.h"
#include "server/core/QcServerWorld.h"
#include "server/mobs/QcMobBreeding.h"
#include "server/mobs/QcMobManager.h"
#include "server/villages/QcVillageGolems.h"
#include "server/villages/QcVillages.h"
#include "world/QcVillageTestUtil.h"

#include <QTest>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

// Two villager species and two golem kinds on the server side (decision E-421): a village whose
// style names the second species is settled by it, only villagers of one species pair and their
// child is of their species, and a village guarded by the second golem kind spawns it below the
// world's limit counted per kind, never counting golems of the other kind it holds.
class TestVillageSpecies : public QObject
{
    Q_OBJECT

private:
    static constexpr qint64 seed = 42;
    // Work time of the routine (QcVillagerSettings::routine).
    static constexpr double workPhase = 0.1;

    struct Village
    {
        std::shared_ptr<QcContentRegistry> content;
        std::unique_ptr<QcServerWorld> world;
        std::unique_ptr<QcMobManager> mobs;
        std::shared_ptr<const QcVillagePlan> plan;
        quint32 nextId = 1;
        double worldTime = QcDayCycle::dayLength * (10.0 + workPhase);

        QcVillage &state() const { return *world->villages().find(plan->id); }
        int type(const QString &name) const { return *content->mobIndex(name); }
    };

    // A copy of the creature kind `from` named `name`; with `settings`, a villager living by them.
    static void addKind(QcContentRegistry &content, const QString &from, const QString &name, const QString &settings = {})
    {
        QcMobDefinition kind = content.mobs()[*content.mobIndex(from)];
        kind.name = name;
        kind.spawnWeight = 0.0;
        kind.extraSpawns.clear();
        kind.villagerSettings = settings;
        kind.villagerSettingsIndex = -1;
        content.registerMob(kind);
    }

    // The base content with the species test:gnome (the villager's settings under its own name) and
    // the golem kinds test:golem_a (the settings') and test:golem_b; the first village of the seed
    // with its style settled by gnomes guarded by test:golem_b, its chunks and a ring around them
    // loaded.
    static Village gnomeVillage()
    {
        QStringList warnings;
        Village result;
        result.content = *QcBaseContent::load(warnings);
        QcContentRegistry &content = *result.content;
        QcVillagerSettings gnomes = *content.villagerSettings(u"qtcraft:villager"_s);
        gnomes.name = u"test:gnome"_s;
        content.registerVillagerSettings(gnomes);
        addKind(content, u"qtcraft:villager"_s, u"test:gnome"_s, gnomes.name);
        addKind(content, u"qtcraft:pig"_s, u"test:golem_a"_s);
        addKind(content, u"qtcraft:pig"_s, u"test:golem_b"_s);
        QcVillageSettings settings = content.villageSettings();
        settings.golem = {u"test:golem_a"_s, 1, 1, 300.0, 8};
        content.setVillageSettings(settings);

        auto generator = std::make_unique<QcTerrainGenerator>(result.content, seed);
        for (int rz = -3; rz <= 3 && !result.plan; ++rz) {
            for (int rx = -3; rx <= 3 && !result.plan; ++rx) {
                std::shared_ptr<const QcVillagePlan> plan = generator->villages().regionPlan(rx, rz);
                if (plan && plan->beds.size() >= 4 && plan->style != u"qtcraft:swamp"_s) {
                    result.plan = std::move(plan);
                }
            }
        }
        if (!result.plan) {
            return result;
        }
        QcVillageStyleDefinition &style = *std::ranges::find(content.villageStyles(), result.plan->style, &QcVillageStyleDefinition::name);
        style.villager = u"test:gnome"_s;
        style.golem = QcVillageGolemPatch{};
        style.golem->mob = u"test:golem_b"_s;

        result.world = std::make_unique<QcServerWorld>(result.content, std::move(generator));
        result.mobs = std::make_unique<QcMobManager>(result.content, seed);
        result.mobs->setVillages(&result.world->villages());
        result.mobs->setGolemLimit(1);
        loadAround(result);
        return result;
    }

    static void loadAround(Village &village)
    {
        const QList<QcChunkPos> chunks = QcVillageTestUtil::chunksOf(*village.plan);
        for (const QcChunkPos &chunk : chunks) {
            for (int dz = -2; dz <= 2; ++dz) {
                for (int dx = -2; dx <= 2; ++dx) {
                    village.world->loadChunk({chunk.x + dx, chunk.z + dz});
                }
            }
        }
        for (const QcChunkPos &chunk : chunks) {
            village.mobs->populateChunk(*village.world, chunk, [&village] { return village.nextId++; });
        }
    }

    static QList<QcCreature *> villagersOf(Village &village)
    {
        QList<QcCreature *> list;
        for (QcCreature &mob : village.mobs->editableMobs()) {
            if (village.content->mobs()[mob.type].villager) {
                list.append(&mob);
            }
        }
        return list;
    }

    static qsizetype countOf(const Village &village, const QString &kind)
    {
        const int type = village.type(kind);
        return std::ranges::count_if(village.mobs->mobs(), [type](const QcCreature &mob) { return mob.type == type; });
    }

    static void ticks(Village &village, int count)
    {
        for (int i = 0; i < count; ++i) {
            village.mobs->tick(*village.world, {}, village.worldTime, QcPhysics::tickDuration, [&village] { return village.nextId++; });
            QcMobBreeding(village.content).tick(*village.world, *village.mobs, QcPhysics::tickDuration);
            village.mobs->villagers().takeActions();
            village.worldTime += QcPhysics::tickDuration;
        }
    }

    // The first two villagers stand at `spot` with food by work time of the next day for one check
    // of village life;
    // the villagers afterwards.
    static qsizetype meet(Village &village, const QcVec3 &spot)
    {
        village.worldTime = QcDayCycle::dayLength * (std::ceil(village.worldTime / QcDayCycle::dayLength) + workPhase);
        const QList<quint32> parents{villagersOf(village).at(0)->id, villagersOf(village).at(1)->id};
        for (const quint32 id : parents) {
            village.mobs->find(id)->villager.produce = 3;
        }
        for (int i = 0; i <= QcMobManager::villageLifeTicks; ++i) {
            for (const quint32 id : parents) {
                QcCreature *parent = village.mobs->find(id);
                if (parent->villager.produce == 3) {
                    parent->motion.position = spot;
                    parent->motion.velocity = {};
                }
            }
            ticks(village, 1);
        }
        return villagersOf(village).size();
    }

private slots:
    void aVillageOfTheSecondSpeciesPairsOnlyWithinIt()
    {
        Village built = gnomeVillage();
        QVERIFY(built.plan);
        built.mobs->setGolemLimit(0);

        // Its villagers are gnomes.
        const QList<QcCreature *> villagers = villagersOf(built);
        QVERIFY(villagers.size() >= 4);
        QVERIFY(std::ranges::all_of(villagers, [&built](const QcCreature *mob) { return mob->type == built.type(u"test:gnome"_s); }));

        // A free bed; a villager of the base species and a gnome do not pair.
        QcCreature *leaving = villagers.constLast();
        const QcVec3 spot = leaving->motion.position;
        built.mobs->remove(leaving->id);
        const qsizetype before = villagersOf(built).size();
        QcCreature *stranger = villagersOf(built).at(1);
        stranger->type = built.type(u"qtcraft:villager"_s);
        QCOMPARE(meet(built, spot), before);

        // Two gnomes do, and their child is a gnome.
        villagersOf(built).at(1)->type = built.type(u"test:gnome"_s);
        QCOMPARE(meet(built, spot), before + 1);
        const QList<QcCreature *> after = villagersOf(built);
        const auto child = std::ranges::find_if(after, [](const QcCreature *mob) { return mob->isYoung(); });
        QVERIFY(child != after.end());
        QCOMPARE((*child)->type, built.type(u"test:gnome"_s));
    }

    void golemsCountTowardsTheLimitOfTheirKind()
    {
        Village built = gnomeVillage();
        QVERIFY(built.plan);

        // A golem of the settings' kind stands in the village: the world holds as many of that kind
        // as allowed, yet the village spawns its own kind, one, as the limit is per kind.
        const QcBlockPos centre = built.plan->centre;
        built.mobs->spawn(built.type(u"test:golem_a"_s), QcVec3{centre.x + 0.5, centre.y + 1.0, centre.z + 0.5}, built.nextId++);
        ticks(built, 3 * QcMobManager::villageLifeTicks + 1);
        QCOMPARE(countOf(built, u"test:golem_b"_s), 1);
        QCOMPARE(countOf(built, u"test:golem_a"_s), 1);
        QCOMPARE(built.state().golems, 1);

        // Counted per kind: the village's own golem does not count for the other kind, and a golem
        // of the other kind in its area counts once.
        const QcVillageGolems golems(built.content);
        QCOMPARE(golems.golemsInWorld(built.mobs->mobs(), built.type(u"test:golem_b"_s), built.world->villages()), 1);
        QCOMPARE(golems.golemsInWorld(built.mobs->mobs(), built.type(u"test:golem_a"_s), built.world->villages()), 1);
        QcVillage stale = built.state();
        stale.golems = 3;
        built.world->villages().restore({stale});
        QCOMPARE(golems.golemsInWorld(built.mobs->mobs(), built.type(u"test:golem_a"_s), built.world->villages()), 1);
        QCOMPARE(golems.golemsInWorld(built.mobs->mobs(), built.type(u"test:golem_b"_s), built.world->villages()), 3);
    }
};

QTEST_GUILESS_MAIN(TestVillageSpecies)
#include "test_village_species.moc"
