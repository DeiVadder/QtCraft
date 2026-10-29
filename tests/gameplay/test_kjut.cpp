// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/gameplay/QcCrafting.h"
#include "common/world/QcDayCycle.h"
#include "QcServerTestFixture.h"
#include "server/animals/QcMobFamilies.h"
#include "server/core/QcServerWorld.h"
#include "server/mobbrains/QcMobHover.h"
#include "server/spawning/QcNaturalSpawner.h"
#include "server/storage/QcEntityCodec.h"
#include "server/storage/QcEntityRecords.h"

#include <QSet>

using namespace Qt::StringLiterals;

namespace {

// A test world whose setup places a Kjut family like the showcase's station (decision E-839).
class FamilySetupGenerator : public QcTestTerrainGenerator
{
public:
    using QcTestTerrainGenerator::QcTestTerrainGenerator;

    QcWorldSetup setup() const override
    {
        QcWorldSetup setup;
        setup.mobs.append({.mob = u"qtcraft:kjut"_s, .at = {8.5, 90.0, 8.5}, .confined = true, .noAi = true, .children = 3});
        return setup;
    }
};

} // namespace

// The Kjut (decision E-839): a parent of spark gel that splits off children when hurt and takes them
// along like a QObject its children: they pop with gunpowder when it dies, vanish quietly when it
// leaves otherwise, wait for a kept parent whose chunk unloads (and go on alone when it died
// meanwhile), and the family survives saving. Only hits that take health split, at most eight in a
// parent's life. Kjuts hover above the ground and water, under low ceilings, children orbit their
// parent; they spawn as monsters in the dark, and spark gel with coal makes four gunpowder. Ticks
// are stepped by hand.
class TestKjut : public QcServerTestFixture
{
    Q_OBJECT

private:
    int kjut() const { return *m_content->mobIndex(u"qtcraft:kjut"_s); }
    int child() const { return *m_content->mobIndex(u"qtcraft:kjut_child"_s); }

    // A creative player (the Kjuts leave it alone) on the stage, without natural spawning.
    void joinStage()
    {
        join();
        loadAround(m_feet, 2);
        chat(u"/gamemode creative"_s);
        m_server->mobs().setNaturalSpawning(false);
        m_server->mobs().removeAll();
    }

    quint32 spawnKjut(double dx, double dz)
    {
        const QcVec3 at{m_feet.x + dx + 0.5, static_cast<double>(m_feet.y), m_feet.z + dz + 0.5};
        return m_server->mobs().spawn(kjut(), at, m_server->entities().newId());
    }

    QList<quint32> childrenOf(quint32 parent) const { return QcMobFamilies::childrenOf(m_server->mobs(), parent); }

    QcServerClient &player() { return *m_server->entities().clients().front(); }

    // A chunk far from the player, loaded, that nothing loads or unloads by itself, and the one east
    // of it.
    QcChunkPos farChunk()
    {
        const QcChunkPos far{m_feet.chunk().x + 24, m_feet.chunk().z};
        m_server->world().loadChunk(far);
        m_server->world().loadChunk({far.x + 1, far.z});
        return far;
    }

    // The middle of `chunk`, high above the ground.
    static QcVec3 middleOf(QcChunkPos chunk) { return {chunk.x * QcWorld::chunkSize + 8.5, 120.0, chunk.z * QcWorld::chunkSize + 8.5}; }

    // A survival player on a flat run of grass from 12 blocks west up to them, air above; a
    // `ceiling` of stone two blocks above the floor with walls makes the run a corridor up to
    // the player.
    void buildRun(bool ceiling)
    {
        join();
        chat(u"/gamemode survival"_s);
        m_server->mobs().setNaturalSpawning(false);
        m_server->mobs().removeAll();
        for (int dx = -12; dx <= 3; ++dx) {
            for (int dz = -2; dz <= 2; ++dz) {
                m_server->world().setBlock(m_feet.offset(dx, -1, dz), id("qtcraft:grass_block"));
                for (int dy = 0; dy <= 4; ++dy) {
                    const bool wall = ceiling && dx < 0 && (dy == 2 || std::abs(dz) == 1);
                    m_server->world().setBlock(m_feet.offset(dx, dy, dz), wall && std::abs(dz) <= 1 ? id("qtcraft:stone") : qcAirBlockId);
                }
            }
        }
    }

    // Hits the creature with the fist after its invulnerability and the player's cooldown; it stands
    // next to the player again first (the blows push it away).
    void hit(quint32 id, const QcVec3 &at)
    {
        idle(12);
        QcCreature *mob = m_server->mobs().find(id);
        QVERIFY(mob);
        mob->motion.position = at;
        mob->motion.velocity = {};
        send(QcMsg::AttackEntity{id});
    }

    QcVec3 beside() const { return {m_feet.x + 1.5, static_cast<double>(m_feet.y), m_feet.z + 0.5}; }

    // A place in the middle of the chunk east of `chunk`, high above the ground (it falls there).
    static QcVec3 eastOf(QcChunkPos chunk)
    {
        return {(chunk.x + 1) * QcWorld::chunkSize + 8.5, 120.0, chunk.z * QcWorld::chunkSize + 8.5};
    }

    // A parent with `count` children split off by hits.
    quint32 family(int count)
    {
        const quint32 parent = spawnKjut(1, 0);
        m_server->mobs().find(parent)->noAi = true;
        for (int i = 0; i < count; ++i) {
            hit(parent, beside());
        }
        ticks(1);
        return parent;
    }

    // The blocks of the stage and the air above it.
    QList<QcBlockId> stageBlocks() const
    {
        QList<QcBlockId> blocks;
        for (int dx = -3; dx <= 3; ++dx) {
            for (int dz = -3; dz <= 3; ++dz) {
                for (int dy = -1; dy <= 3; ++dy) {
                    blocks.append(*m_server->world().blockAt(m_feet.offset(dx, dy, dz)));
                }
            }
        }
        return blocks;
    }

    QcEntityRecord throughTheCodec(const QcEntityRecord &record) const
    {
        QcLoadWarnings warnings;
        QcResult<QcEntityRecord> decoded = QcEntityCodec::decode(QcEntityCodec::encode(record, *m_content), record.chunk, *m_content, warnings);
        if (!decoded) {
            QcTestUtil::failCase(QString::fromUtf8(qcErrorText(decoded)));
            return {};
        }
        return *decoded;
    }

    // Takes the creatures of `chunks` out as if they unloaded; their records after saving and loading.
    QList<QcEntityRecord> unload(const QSet<QcChunkPos> &chunks)
    {
        QList<QcEntityRecord> records;
        const QHash<QcChunkPos, QcEntityRecord> taken = QcEntityRecords::take(m_server->entities(), chunks);
        for (const QcEntityRecord &record : taken) {
            records.append(throughTheCodec(record));
        }
        return records;
    }

    void load(const QList<QcEntityRecord> &records)
    {
        for (const QcEntityRecord &record : records) {
            QVERIFY(QcEntityRecords::apply(record, m_server->entities()).isEmpty());
        }
    }

    // West of the player a basin of stone two blocks deep (the hills of the test terrain may lie
    // lower) from 9 to 2 blocks away, full of `liquid`; its surface is level with the ground.
    void buildPool(const char *liquid)
    {
        for (int dx = -10; dx <= -1; ++dx) {
            for (int dz = -3; dz <= 3; ++dz) {
                const bool inside = dx > -10 && dx < -1 && std::abs(dz) < 3;
                m_server->world().setBlock(m_feet.offset(dx, -3, dz), id("qtcraft:stone"));
                m_server->world().setBlock(m_feet.offset(dx, -2, dz), inside ? id(liquid) : id("qtcraft:stone"));
                m_server->world().setBlock(m_feet.offset(dx, -1, dz), inside ? id(liquid) : id("qtcraft:stone"));
            }
        }
    }

    // The highest a Kjut's feet were above `floor` and the lowest (after half a second to rise from
    // where it spawned), over `count` ticks of the player standing still, and whether it ever touched
    // the ground or the water.
    struct Flight
    {
        double lowest = 1e9;
        double highest = -1e9;
        bool touched = false;
    };
    Flight follow(quint32 id, double floor, int count)
    {
        Flight flight;
        for (int i = 0; i < count && m_server->mobs().find(id); ++i) {
            idle(1);
            const QcCreature &mob = *m_server->mobs().find(id);
            if (i >= 10) {
                flight.lowest = std::min(flight.lowest, mob.motion.position.y - floor);
            }
            flight.highest = std::max(flight.highest, mob.motion.position.y - floor);
            flight.touched = flight.touched || mob.motion.onGround || mob.motion.inWater;
        }
        return flight;
    }

private slots:
    void theContentDefinesTheFamily()
    {
        const QcMobDefinition &parent = m_content->mobs()[kjut()];
        QVERIFY(parent.family && parent.hostile && parent.hover && parent.spawnsNaturally());
        QCOMPARE(parent.family->childType, child());
        QCOMPARE(parent.family->maxChildren, 4);
        QCOMPARE(parent.family->maxSplits, 8);
        QVERIFY(parent.hover->height > 0.0 && parent.hover->maxHeight <= 3.0);
        // Hovering, it fits under a ceiling two blocks above the floor.
        QVERIFY(parent.hover->height + parent.hover->bob + parent.height < 2.0);
        QCOMPARE(parent.family->popDrops.size(), 1);
        QCOMPARE(parent.family->popDrops.first().item, u"qtcraft:gunpowder"_s);
        QVERIFY(QcNaturalSpawner::isMonster(parent));
        QVERIFY(parent.spawnRules().constFirst().darkness);
        QCOMPARE(parent.spawnRules().constFirst().maxLight, 0);
        const QcMobDefinition &little = m_content->mobs()[child()];
        QVERIFY(!little.family && little.hostile && little.hover && !little.spawnsNaturally() && little.width < parent.width);
        QVERIFY(std::ranges::all_of(little.drops, [](const QcItemDrop &drop) { return drop.item == u"qtcraft:spark_gel"_s; }));

        // The showcase's station places a parent with children.
        const auto station = std::ranges::find(m_content->worldgen().structures(), u"qtcraft:kjut_family"_s, &QcStructureDefinition::name);
        QVERIFY(station != m_content->worldgen().structures().end());
        QCOMPARE(station->mobs.size(), 1);
        QCOMPARE(station->mobs.first().children, 3);
    }

    // Spark gel and coal make four gunpowder; coal and flint still make two.
    void sparkGelAndCoalMakeFourGunpowder()
    {
        const auto recipe = [this](const QString &name) { return m_content->recipes().at(*m_content->recipeIndex(name)); };
        const QcRecipeDefinition gel = recipe(u"qtcraft:gunpowder_from_spark_gel"_s);
        QCOMPARE(gel.resultItem, u"qtcraft:gunpowder"_s);
        QCOMPARE(gel.resultCount, 4);
        const QList<std::pair<QString, int>> needs = QcCrafting::requirements(gel);
        QCOMPARE(needs.size(), 2);
        QVERIFY(needs.contains(std::pair(u"qtcraft:spark_gel"_s, 1)) && needs.contains(std::pair(u"qtcraft:coal"_s, 1)));
        QCOMPARE(recipe(u"qtcraft:gunpowder"_s).resultCount, 2);
    }

    void aHitSplitsOffAChildUpToFour()
    {
        joinStage();
        const quint32 parent = spawnKjut(1, 0);
        m_server->mobs().find(parent)->noAi = true;
        hit(parent, beside());
        ticks(1);
        QCOMPARE(childrenOf(parent).size(), 1);
        const QcCreature *first = m_server->mobs().find(childrenOf(parent).constFirst());
        QCOMPARE(first->type, child());
        QVERIFY(!first->persistent && first->parentUid == 0);

        for (int i = 0; i < 5; ++i) {
            hit(parent, beside());
        }
        ticks(1);
        QCOMPARE(childrenOf(parent).size(), 4);
        // Every child is linked to the parent; none to another.
        QCOMPARE(std::ranges::count(m_server->mobs().mobs(), child(), &QcCreature::type), 4);
    }

    // A hit without damage (a snowball, an egg) opens no invulnerability and splits nothing, however
    // often; a hit that takes health splits.
    void aHitWithoutDamageDoesNotSplit()
    {
        joinStage();
        const quint32 parent = spawnKjut(1, 0);
        m_server->mobs().find(parent)->noAi = true;
        for (int i = 0; i < 6; ++i) {
            QVERIFY(!m_server->mobs().damage({.creature = parent, .damage = 0.0, .from = beside(), .knockback = 0.0}));
            ticks(1);
        }
        QVERIFY(childrenOf(parent).isEmpty());
        QVERIFY(!m_server->mobs().damage({.creature = parent, .damage = 1.0, .from = beside(), .knockback = 0.0}));
        ticks(1);
        QCOMPARE(childrenOf(parent).size(), 1);
    }

    // A stronger hit during the invulnerability takes the difference but splits nothing: only a
    // fresh hit splits, by a blow or by wounds (fire, cramming).
    void aStrongerHitDuringTheWindowDoesNotSplit()
    {
        joinStage();
        const quint32 parent = spawnKjut(1, 0);
        m_server->mobs().find(parent)->noAi = true;
        QVERIFY(!m_server->mobs().damage({.creature = parent, .damage = 1.0, .from = beside(), .knockback = 0.0}));
        ticks(1);
        QCOMPARE(childrenOf(parent).size(), 1);
        const double health = m_server->mobs().find(parent)->health;
        QVERIFY(!m_server->mobs().damage({.creature = parent, .damage = 3.0, .from = beside(), .knockback = 0.0}));
        ticks(1);
        QVERIFY(!m_server->mobs().wound(parent, 4.0));
        ticks(1);
        QCOMPARE(m_server->mobs().find(parent)->health, health - 3.0);
        QCOMPARE(childrenOf(parent).size(), 1);

        // After the window a wound is a fresh hit again.
        idle(12);
        QVERIFY(!m_server->mobs().wound(parent, 1.0));
        ticks(1);
        QCOMPARE(childrenOf(parent).size(), 2);
    }

    // However many children it loses, a parent splits off no more than eight in its life.
    void aParentSplitsAtMostEightInItsLife()
    {
        joinStage();
        const quint32 parent = spawnKjut(1, 0);
        QSet<quint32> split;
        for (int round = 0; round < 20; ++round) {
            QcCreature &mob = *m_server->mobs().find(parent);
            mob.noAi = true;
            mob.health = m_content->mobs()[kjut()].maxHealth;
            mob.hurt.clear();
            QVERIFY(!m_server->mobs().damage({.creature = parent, .damage = 1.0, .from = beside(), .knockback = 0.0}));
            ticks(1);
            // Its children are killed off at once, so there is always room for more.
            const QList<quint32> children = childrenOf(parent);
            for (const quint32 id : children) {
                split.insert(id);
                QVERIFY(m_server->mobs().remove(id));
            }
        }
        QCOMPARE(split.size(), 8);
        QCOMPARE(m_server->mobs().find(parent)->splits, 8);
    }

    // The parent dies: all children pop in the same tick with one gunpowder each, the parent leaves
    // its spark gel, and no block changes (decision E-839: no explosion).
    void theParentsDeathPopsAllChildren()
    {
        joinStage();
        const quint32 parent = family(3);
        QCOMPARE(childrenOf(parent).size(), 3);
        const QList<QcBlockId> before = stageBlocks();
        m_server->items().editableItems().clear();

        m_server->mobs().find(parent)->health = 0.5;
        hit(parent, beside());
        ticks(1);
        QVERIFY(!m_server->mobs().find(parent));
        QCOMPARE(std::ranges::count(m_server->mobs().mobs(), child(), &QcCreature::type), 0);
        QCOMPARE(m_server->creatures().families().popped(), 3);
        QCOMPARE(droppedCount(item("qtcraft:gunpowder")), 3);
        const int gel = droppedCount(item("qtcraft:spark_gel"));
        QVERIFY2(gel >= 1 && gel <= 2, qPrintable(QString::number(gel)));
        QCOMPARE(stageBlocks(), before);
    }

    // A child killed on its own leaves only spark gel (or nothing); its siblings and parent stay.
    void aChildKilledAloneLeavesOnlyGel()
    {
        joinStage();
        const quint32 parent = family(3);
        m_server->items().editableItems().clear();
        const quint32 little = childrenOf(parent).constFirst();
        m_server->mobs().find(little)->health = 0.5;
        hit(little, beside());
        ticks(1);
        QVERIFY(!m_server->mobs().find(little));
        QCOMPARE(childrenOf(parent).size(), 2);
        QCOMPARE(droppedCount(item("qtcraft:gunpowder")), 0);
        QVERIFY(droppedCount(item("qtcraft:spark_gel")) <= 1);
        QCOMPARE(m_server->creatures().families().popped(), 0);
    }

    // A parent that leaves without a death (removed, despawned) takes its children quietly.
    void aParentLeavingWithoutDeathTakesItsChildrenQuietly()
    {
        joinStage();
        const quint32 parent = family(2);
        m_server->items().editableItems().clear();
        QVERIFY(m_server->mobs().remove(parent));
        ticks(1);
        QCOMPARE(std::ranges::count(m_server->mobs().mobs(), child(), &QcCreature::type), 0);
        QCOMPARE(m_server->creatures().families().popped(), 0);
        QVERIFY(m_server->items().items().isEmpty());
    }

    // The chunk of a parent that is not kept unloads: it is gone for good, and its children in
    // another, still loaded chunk vanish with it.
    void childrenOfAnUnloadedParentVanish()
    {
        joinStage();
        const quint32 parent = family(2);
        const QcChunkPos home = QcEntities::chunkOf(*m_server->mobs().find(parent));
        const QList<quint32> children = childrenOf(parent);
        for (const quint32 id : children) {
            m_server->mobs().find(id)->motion.position = eastOf(home);
        }
        unload({home});
        ticks(1);
        QCOMPARE(std::ranges::count(m_server->mobs().mobs(), child(), &QcCreature::type), 0);
        QCOMPARE(m_server->creatures().families().popped(), 0);
    }

    // A kept family (a structure's, or kept by a command): the children are kept with it and wait
    // for their parent while its chunk is not loaded; it comes back and they are linked again.
    void aKeptFamilyWaitsForItsParentsChunk()
    {
        joinStage();
        const quint32 parent = family(2);
        m_server->mobs().keep(*m_server->mobs().find(parent), true);
        const quint64 uid = m_server->mobs().find(parent)->uid;
        QVERIFY(uid != 0);
        // The family far from the player, where nothing loads the parent's chunk again by itself.
        const QcChunkPos home = farChunk();
        m_server->mobs().find(parent)->motion.position = middleOf(home);
        const QList<quint32> children = childrenOf(parent);
        for (const quint32 id : children) {
            QcCreature &little = *m_server->mobs().find(id);
            QVERIFY(little.persistent);
            QCOMPARE(little.parentUid, uid);
            little.motion.position = eastOf(home);
        }
        ticks(1);

        const QList<QcEntityRecord> records = unload({home});
        m_server->world().unloadChunk(home);
        ticks(2);
        QList<const QcCreature *> waiting;
        for (const QcCreature &mob : m_server->mobs().mobs()) {
            if (mob.type == child()) {
                waiting.append(&mob);
                QVERIFY(mob.parent == 0 && mob.parentUid == uid && mob.persistent);
                QCOMPARE(mob.parentChunk, std::optional<QcChunkPos>(home));
            }
        }
        QCOMPARE(waiting.size(), 2);

        m_server->world().loadChunk(home);
        load(records);
        ticks(1);
        const auto back = std::ranges::find(m_server->mobs().mobs(), kjut(), &QcCreature::type);
        QVERIFY(back != m_server->mobs().mobs().end());
        QCOMPARE(back->uid, uid);
        QCOMPARE(childrenOf(back->id).size(), 2);
    }

    // A kept child whose parent died while the child was unloaded: once it is back and the chunk the
    // parent was last in is loaded without the parent, it goes on alone, no longer kept.
    void aKeptChildWhoseParentDiedGoesOnAlone()
    {
        joinStage();
        const quint32 parent = family(2);
        m_server->mobs().keep(*m_server->mobs().find(parent), true);
        ticks(1);
        const QcChunkPos home = QcEntities::chunkOf(*m_server->mobs().find(parent));
        // Near enough that the released children do not despawn at once.
        const QcChunkPos away{home.x + 2, home.z};
        const QList<quint32> children = childrenOf(parent);
        for (const quint32 id : children) {
            QCOMPARE(m_server->mobs().find(id)->parentChunk, std::optional<QcChunkPos>(home));
            m_server->mobs().find(id)->motion.position = middleOf(away);
        }
        const QList<QcEntityRecord> records = unload({away});

        m_server->mobs().find(parent)->health = 0.5;
        hit(parent, beside());
        ticks(2);
        QVERIFY(!m_server->mobs().find(parent));
        QCOMPARE(m_server->creatures().families().popped(), 0);

        load(records);
        ticks(1);
        QCOMPARE(std::ranges::count(m_server->mobs().mobs(), child(), &QcCreature::type), 2);
        for (const QcCreature &mob : m_server->mobs().mobs()) {
            QVERIFY(mob.parent == 0 && mob.parentUid == 0 && !mob.persistent && !mob.parentChunk);
        }
    }

    // No more children than `maxChildren` link to one parent, whatever the saves hold.
    void aParentTakesBackNoMoreThanFour()
    {
        joinStage();
        const quint32 parent = family(4);
        m_server->mobs().keep(*m_server->mobs().find(parent), true);
        const quint64 uid = m_server->mobs().find(parent)->uid;
        // A stray kept child of the same parent, as if from a copied world.
        const quint32 stray = m_server->mobs().spawn(child(), beside(), m_server->entities().newId(), false, true);
        m_server->mobs().find(stray)->parentUid = uid;
        m_server->mobs().relinkFamilies();
        QCOMPARE(childrenOf(parent).size(), 4);
        const QcCreature &released = *m_server->mobs().find(stray);
        QVERIFY(released.parent == 0 && released.parentUid == 0 && !released.persistent);
    }

    // A kept family saved with its chunk and loaded again: the same parent with its children, which
    // pop when it dies.
    void aKeptFamilySurvivesSavingAndLoading()
    {
        joinStage();
        const quint32 parent = family(3);
        m_server->mobs().keep(*m_server->mobs().find(parent), true);
        const QcChunkPos home = QcEntities::chunkOf(*m_server->mobs().find(parent));
        QSet<QcChunkPos> chunks{home};
        const QList<quint32> children = childrenOf(parent);
        for (const quint32 id : children) {
            chunks.insert(QcEntities::chunkOf(*m_server->mobs().find(id)));
        }

        const QList<QcEntityRecord> records = unload(chunks);
        QVERIFY(m_server->mobs().mobs().isEmpty());
        load(records);
        ticks(1);
        const auto back = std::ranges::find(m_server->mobs().mobs(), kjut(), &QcCreature::type);
        QVERIFY(back != m_server->mobs().mobs().end());
        const quint32 loaded = back->id;
        QCOMPARE(childrenOf(loaded).size(), 3);
        const QList<quint32> loadedChildren = childrenOf(loaded);
        for (const quint32 id : loadedChildren) {
            QCOMPARE(m_server->mobs().find(id)->parentUid, back->uid);
        }

        m_server->items().editableItems().clear();
        QcCreature *mob = m_server->mobs().find(loaded);
        mob->noAi = true;
        mob->health = 0.5;
        hit(loaded, beside());
        ticks(1);
        QCOMPARE(m_server->creatures().families().popped(), 3);
        QCOMPARE(droppedCount(item("qtcraft:gunpowder")), 3);
    }

    // A world setup (the showcase's station) places a kept, confined parent with its children.
    void aWorldSetupPlacesAFamily()
    {
        join({}, std::make_unique<FamilySetupGenerator>(m_content));
        const auto parent = std::ranges::find(m_server->mobs().mobs(), kjut(), &QcCreature::type);
        QVERIFY(parent != m_server->mobs().mobs().end());
        QVERIFY(parent->persistent && parent->confined && parent->noAi);
        const QList<quint32> children = childrenOf(parent->id);
        QCOMPARE(children.size(), 3);
        // The children keep a mind, so they orbit the parent without one.
        for (const quint32 id : children) {
            const QcCreature &little = *m_server->mobs().find(id);
            QVERIFY(little.persistent && little.confined && !little.noAi);
            QCOMPARE(little.parentUid, parent->uid);
        }
    }

    // A Kjut hovers after a player above the ground (never touching it, never higher than 3 m),
    // reaches them and strikes.
    void aKjutHoversToAPlayerAndStrikes()
    {
        buildRun(false);
        const quint32 id = spawnKjut(-10, 0);
        const Flight flight = follow(id, m_feet.y, 200);
        QVERIFY(!flight.touched);
        QVERIFY2(flight.lowest > 0.2 && flight.highest < 3.0, qPrintable(u"%1 %2"_s.arg(flight.lowest).arg(flight.highest)));
        QVERIFY2(player().health < QcServerClient::maxHealth, qPrintable(QString::number(player().health)));
        const QcCreature &mob = *m_server->mobs().find(id);
        QVERIFY(std::abs(mob.motion.position.x - (m_feet.x + 0.5)) < 3.0);
    }

    // Down a corridor two blocks high the Kjut hovers lower and never gets stuck under the ceiling.
    void aKjutPassesALowCorridor()
    {
        buildRun(true);
        const quint32 id = spawnKjut(-10, 0);
        const Flight flight = follow(id, m_feet.y, 200);
        const QcMobDefinition &kind = m_content->mobs()[kjut()];
        QVERIFY2(flight.highest + kind.height <= 2.0 + 1e-6, qPrintable(QString::number(flight.highest)));
        QVERIFY(!flight.touched);
        QVERIFY2(player().health < QcServerClient::maxHealth, qPrintable(QString::number(player().health)));
    }

    // Over a pond it hovers above the surface and never goes into the water.
    void aKjutHoversOverWater()
    {
        buildRun(false);
        buildPool("qtcraft:water");
        const quint32 id = spawnKjut(-11, 0);
        const Flight flight = follow(id, m_feet.y, 200);
        QVERIFY(!flight.touched);
        QVERIFY2(flight.lowest > 0.2, qPrintable(QString::number(flight.lowest)));
        const QcCreature &mob = *m_server->mobs().find(id);
        QVERIFY(std::abs(mob.motion.position.x - (m_feet.x + 0.5)) < 3.0);
    }

    // Wounds without a push (fire, cramming) knock a Kjut back without lifting it: hurt over and
    // over above lava, it never sinks below its hover height towards the surface.
    void aWoundedKjutStaysAboveLava()
    {
        buildRun(false);
        buildPool("qtcraft:lava");
        const quint32 id = spawnKjut(-5, 0);
        m_server->mobs().find(id)->noAi = true;
        idle(20);
        double lowest = 1e9;
        for (int i = 0; i < 120 && m_server->mobs().find(id); ++i) {
            QVERIFY(!m_server->mobs().wound(id, 0.5));
            idle(1);
            lowest = std::min(lowest, m_server->mobs().find(id)->motion.position.y - m_feet.y);
        }
        QVERIFY(m_server->mobs().find(id));
        QVERIFY2(lowest > 0.35, qPrintable(QString::number(lowest)));
    }

    // A child split off pops up from the parent's middle and sinks back to its orbit over a few
    // ticks.
    void aSplitChildPopsUp()
    {
        joinStage();
        const quint32 parent = spawnKjut(1, 0);
        m_server->mobs().find(parent)->noAi = true;
        idle(20);
        hit(parent, m_server->mobs().find(parent)->motion.position);
        ticks(1);
        QCOMPARE(childrenOf(parent).size(), 1);
        // It was split off around the middle of the parent's body.
        const quint32 little = childrenOf(parent).constFirst();
        const double start = m_server->mobs().find(parent)->motion.position.y
                             + (m_content->mobs()[kjut()].height - m_content->mobs()[child()].height) * 0.5;
        double highest = start;
        for (int i = 0; i < 10; ++i) {
            ticks(1);
            highest = std::max(highest, m_server->mobs().find(little)->motion.position.y);
        }
        QVERIFY2(highest - start > 0.5, qPrintable(QString::number(highest - start)));
        idle(40);
        const double above = m_server->mobs().find(little)->motion.position.y - m_server->mobs().find(parent)->motion.position.y;
        QVERIFY2(std::abs(above) < 0.8, qPrintable(QString::number(above)));
    }

    // Kjuts chasing the same player push apart instead of merging into one body.
    void chasingKjutsKeepApart()
    {
        buildRun(false);
        QList<quint32> kjuts;
        for (int i = 0; i < 2; ++i) {
            kjuts.append(spawnKjut(-5, 0));
        }
        double closest = 1e9;
        for (int i = 0; i < 70; ++i) {
            idle(1);
            for (qsizetype a = 0; i >= 50 && a < kjuts.size(); ++a) {
                for (qsizetype b = a + 1; b < kjuts.size(); ++b) {
                    const QcVec3 apart = m_server->mobs().find(kjuts[a])->motion.position - m_server->mobs().find(kjuts[b])->motion.position;
                    closest = std::min(closest, std::hypot(apart.x, apart.z));
                }
            }
        }
        QVERIFY2(closest > 0.4, qPrintable(QString::number(closest)));
        QVERIFY(m_server->mobs().find(kjuts.constFirst())->chasing != 0);
    }

    // An orbit point below the floor (a parent low over a pond) never takes a child into the water:
    // its feet stay on the surface or above.
    void aChildNeverFollowsItsOrbitIntoTheWater()
    {
        buildRun(false);
        buildPool("qtcraft:water");
        const QcMobDefinition &kind = m_content->mobs()[child()];
        QcCreature mob;
        mob.id = 1;
        mob.type = child();
        mob.motion.position = {m_feet.x - 4.5, m_feet.y + 0.5, m_feet.z + 0.5};
        const QcMobHover::Course course{{}, static_cast<double>(m_feet.y) - 2.0};
        for (int i = 0; i < 40; ++i) {
            QcMobHover::fly(mob, kind, m_server->world(), course, i * QcPhysics::tickDuration);
            QVERIFY2(mob.motion.position.y >= m_feet.y - 1e-6, qPrintable(QString::number(mob.motion.position.y - m_feet.y)));
        }
    }

    // Children that chase nobody (the player is in creative mode) orbit their parent: around the
    // middle of its body within their radius, going round as the time passes.
    void childrenOrbitTheirParent()
    {
        joinStage();
        const quint32 parent = family(3);
        const QcFamilyDefinition &family = *m_content->mobs()[kjut()].family;
        const QList<quint32> children = childrenOf(parent);
        idle(60);
        QHash<quint32, QcVec3> before;
        for (const quint32 id : children) {
            before.insert(id, m_server->mobs().find(id)->motion.position);
        }
        for (int i = 0; i < 20; ++i) {
            idle(1);
            const QcVec3 centre = m_server->mobs().find(parent)->motion.position;
            for (const quint32 id : children) {
                const QcVec3 at = m_server->mobs().find(id)->motion.position;
                const double radius = std::hypot(at.x - centre.x, at.z - centre.z);
                QVERIFY2(radius > family.orbitRadius * 0.5 && radius < family.orbitRadius * 1.5, qPrintable(QString::number(radius)));
                QVERIFY(std::abs(at.y - centre.y) < 1.0);
            }
        }
        // A second later each has gone a good way round.
        for (const quint32 id : children) {
            const QcVec3 moved = m_server->mobs().find(id)->motion.position - before.value(id);
            QVERIFY2(moved.length() > 0.5, qPrintable(QString::number(moved.length())));
        }
    }

    // The monsters' spawning picks Kjuts in the dark at night, never in daylight in the open.
    void kjutsSpawnAtNightInTheDark()
    {
        const auto count = [this](double time) {
            QcServerWorld world(m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
            for (int x = -4; x <= 4; ++x) {
                for (int z = -4; z <= 4; ++z) {
                    world.loadChunk({x, z});
                }
            }
            const QcBlockPos column{0, 0, 0};
            const int top = world.chunk(column.chunk())->highestNonAir(column.localX(), column.localZ());
            QcMobManager mobs(m_content, 9);
            const QList<QcMobManager::PlayerView> players{{7, {0.5, static_cast<double>(top + 1), 0.5}}};
            quint32 next = 100;
            QSet<quint32> kjuts;
            for (int tick = 0; tick < 20 * 30; ++tick) {
                mobs.tick(world, players, time, 0.05, [&next] { return next++; });
                for (const QcCreature &mob : mobs.mobs()) {
                    if (mob.type == kjut()) {
                        kjuts.insert(mob.id);
                    }
                }
            }
            return kjuts.size();
        };
        QVERIFY(count(QcDayCycle::dayLength * 0.75) > 0);
        QCOMPARE(count(QcDayCycle::dayLength * 0.25), 0);
    }
};

QTEST_GUILESS_MAIN(TestKjut)
#include "test_kjut.moc"
