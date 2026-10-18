#include "gameplay/QcEndermanFixture.h"
#include "server/storage/QcEntityCodec.h"
#include "server/storage/QcEntityRecords.h"

using namespace Qt::StringLiterals;

// Griefing and minds per creature (decision E-510): a confined creature changes no block through
// any path (an enderman takes and places nothing and drops what it carries, a silverfish neither
// hides nor breaks infested blocks when it wakes its kind), and a creature without a mind (noAi)
// neither steers, attacks nor teleports, yet falls and is knocked back, is saved, and comes by
// /summon … noai and /mob … noai. Ticks are stepped by hand.
class TestCreatureMinds : public QcEndermanFixture
{
    Q_OBJECT

private:
    QcBlockId at(QcBlockPos pos) const { return *m_server->world().blockAt(pos); }

    qsizetype countOf(const char *kind) const
    {
        return std::ranges::count(m_server->entities().creatures().mobs(), type(kind), &QcCreature::type);
    }

    // Ticks until the creature carries `block` (air: none); false on time-out.
    bool carriesWithin(quint32 creature, QcBlockId block, int maxTicks)
    {
        for (int tick = 0; tick < maxTicks && mob(creature) && mob(creature)->carried != block; ++tick) {
            ticks(1);
        }
        return mob(creature) && mob(creature)->carried == block;
    }

    // Hurts the creature a little (healed first, so it lives) and steps one tick.
    void hurt(quint32 creature)
    {
        QcCreature *hurtOne = mob(creature);
        QVERIFY(hurtOne);
        hurtOne->health = 8.0;
        hurtOne->hurt.clear();
        m_server->entities().creatures().damage({.creature = creature, .damage = 1.0, .from = hurtOne->motion.position});
        ticks(1);
    }

    // Saves the chunk of the creature through the entity codec and loads it again; the creature of
    // `kind` that came back.
    const QcCreature *saveAndLoad(quint32 creature, const char *kind)
    {
        const QcChunkPos chunk = QcEntities::chunkOf(*mob(creature));
        const QHash<QcChunkPos, QcEntityRecord> records = QcEntityRecords::take(m_server->entities(), {chunk});
        QStringList warnings;
        QSet<QString> reported;
        const QcResult<QcEntityRecord> back = QcEntityCodec::decode(QcEntityCodec::encode(records[chunk], *m_content), chunk, *m_content, warnings, reported);
        if (!back.has_value() || !QcEntityRecords::apply(*back, m_server->entities()).isEmpty()) {
            return nullptr;
        }
        const auto loaded = std::ranges::find(m_server->entities().creatures().mobs(), type(kind), &QcCreature::type);
        return loaded == m_server->entities().creatures().mobs().end() ? nullptr : &*loaded;
    }

private slots:
    void confinedEndermanTakesAndPlacesNothing()
    {
        setCarryChances(1.0, 1.0);
        joinArena();
        chat(u"/gamemode creative"_s);
        const QcBlockId grass = id("qtcraft:grass_block");
        const quint32 free = spawn("enderman", 8, 8);
        const quint32 penned = spawn("enderman", -8, -8, true);

        // The free one takes a block; the confined one, under the same rule and chances, none.
        QVERIFY(carriesWithin(free, grass, 400));
        ticks(200);
        QVERIFY(mob(penned));
        QCOMPARE(mob(penned)->carried, qcAirBlockId);

        // One that carries a block when confined drops it as an item instead of placing it.
        m_server->entities().creatures().carryBlock(*mob(penned), grass);
        const int dropped = droppedCount(item("qtcraft:grass_block"));
        QVERIFY(carriesWithin(penned, qcAirBlockId, 400));
        QCOMPARE(droppedCount(item("qtcraft:grass_block")), dropped + 1);
    }

    void confinedSilverfishNeitherHidesNorBreaksBlocks()
    {
        joinArena();
        chat(u"/gamemode creative"_s);
        m_server->entities().creatures().removeAll();
        for (int dx = -arena; dx <= arena; ++dx) {
            for (int dz = -arena; dz <= arena; ++dz) {
                m_server->world().setBlock(m_feet.offset(dx, -1, dz), id("qtcraft:stone"));
            }
        }

        // With the rule on a free silverfish hides in the stone; a confined one stays out.
        const quint32 free = spawn("silverfish", 12, 12);
        const quint32 penned = spawn("silverfish", -12, -12, true);
        ticks(400);
        QVERIFY(!mob(free));
        QVERIFY(mob(penned));

        // Hurt, it wakes nobody: the infested block next to it turns back into its base block.
        const QcBlockId infested = id("qtcraft:infested_stone_bricks");
        const QcBlockPos near = QcPhysics::blockContaining(mob(penned)->motion.position).offset(2, -1, 0);
        m_server->world().setBlock(near, infested);
        for (int i = 0; i < 20 && at(near) == infested; ++i) {
            hurt(penned);
        }
        QCOMPARE(at(near), id("qtcraft:stone_bricks"));
        QCOMPARE(countOf("silverfish"), 1);
    }

    void creatureWithoutMindNeitherMovesNorAttacks()
    {
        joinArena();
        chat(u"/gamemode survival"_s);
        m_server->entities().creatures().removeAll();
        const quint32 zombie = spawn("zombie", 2, 0);
        chat(u"/mob "_s + QString::number(zombie) + u" noai on"_s);
        QVERIFY(mob(zombie)->noAi && mob(zombie)->persistent);
        const QcVec3 start = mob(zombie)->motion.position;
        const double health = player().health;

        ticks(200);
        const QcVec3 offset = mob(zombie)->motion.position - start;
        QVERIFY2(std::hypot(offset.x, offset.z) < 0.01, qPrintable(QString::number(std::hypot(offset.x, offset.z))));
        QCOMPARE(player().health, health);

        // With its mind back it attacks; it stays kept with the world.
        chat(u"/mob nearest noai off"_s);
        QVERIFY(!mob(zombie)->noAi && mob(zombie)->persistent);
        for (int tick = 0; tick < 200 && player().health >= health; ++tick) {
            ticks(1);
        }
        QVERIFY(player().health < health);
    }

    void creatureWithoutMindFallsAndIsPushed()
    {
        joinArena();
        chat(u"/gamemode creative"_s);
        const QcVec3 high{m_feet.x + 4.5, m_feet.y + 6.0, m_feet.z + 4.5};
        const quint32 pig = m_server->entities().creatures().spawn(type("pig"), high, m_server->entities().newId());
        mob(pig)->noAi = true;
        ticks(60);
        QCOMPARE(mob(pig)->motion.position.y, double(m_feet.y));
        QVERIFY(mob(pig)->motion.onGround);

        // A blow lifts it off the ground.
        const QcVec3 before = mob(pig)->motion.position;
        QVERIFY(!m_server->entities().creatures().damage({.creature = pig, .damage = 1.0, .from = before - QcVec3{1.0, 0.0, 0.0}}));
        ticks(2);
        QVERIFY(mob(pig)->motion.position.y > before.y + 0.1);
        ticks(40);

        // The crowd pushes it aside: a pig in its place shoves it.
        const QcVec3 rest = mob(pig)->motion.position;
        m_server->entities().creatures().spawn(type("pig"), rest + QcVec3{0.1, 0.0, 0.0}, m_server->entities().newId());
        ticks(20);
        const QcVec3 offset = mob(pig)->motion.position - rest;
        QVERIFY2(std::hypot(offset.x, offset.z) > 0.1, qPrintable(QString::number(std::hypot(offset.x, offset.z))));
    }

    void creatureWithoutMindNeitherTeleportsNorTakesBlocks()
    {
        setCarryChances(1.0, 1.0);
        joinArena();
        chat(u"/gamemode creative"_s);
        const quint32 enderman = spawn("enderman", 6, 6);
        mob(enderman)->noAi = true;
        QVERIFY(!m_server->entities().creatures().teleport(enderman, m_server->world()));
        ticks(400);
        QCOMPARE(mob(enderman)->carried, qcAirBlockId);
        // Confined, it does not teleport either; free, it does.
        mob(enderman)->noAi = false;
        mob(enderman)->confined = true;
        QVERIFY(!m_server->entities().creatures().teleport(enderman, m_server->world()));
        mob(enderman)->confined = false;
        mob(enderman)->teleportCooldown = 0;
        QVERIFY(m_server->entities().creatures().teleport(enderman, m_server->world()));
    }

    void creatureWithoutMindIsSaved()
    {
        joinArena();
        m_server->entities().creatures().removeAll();
        const quint32 pig = spawn("pig", -5, 5);
        chat(u"/mob "_s + QString::number(pig) + u" noai on"_s);
        const QcCreature *loaded = saveAndLoad(pig, "pig");
        QVERIFY(loaded);
        QVERIFY(loaded->noAi && loaded->persistent && !loaded->confined);

        // One with its mind back is saved without the mark.
        const quint32 again = loaded->id;
        chat(u"/mob "_s + QString::number(again) + u" noai off"_s);
        const QcCreature *back = saveAndLoad(again, "pig");
        QVERIFY(back);
        QVERIFY(!back->noAi && back->persistent);
    }

    void summonAndMobCommandsSetTheMind()
    {
        joinArena();
        m_server->entities().creatures().removeAll();
        player().yaw = 0.0f;
        player().pitch = 0.0f;
        chat(u"/summon pig 2 noai"_s);
        chat(u"/summon sheep noai"_s);
        chat(u"/summon chicken"_s);
        QCOMPARE(countOf("pig"), 2);
        QCOMPARE(countOf("sheep"), 1);
        QCOMPARE(countOf("chicken"), 1);
        for (const QcCreature &creature : m_server->entities().creatures().mobs()) {
            const bool chicken = creature.type == type("chicken");
            QCOMPARE(creature.noAi, !chicken);
            QVERIFY(creature.summoned);
            QVERIFY(chicken || creature.persistent);
        }

        // The one in view loses its mind; a word that is no creature changes nothing.
        const auto chicken = std::ranges::find(m_server->entities().creatures().mobs(), type("chicken"), &QcCreature::type);
        const quint32 chickenId = chicken->id;
        mob(chickenId)->motion.position = {m_feet.x + 5.5, double(m_feet.y), m_feet.z + 0.5};
        ticks(1);
        lookAt(mob(chickenId)->motion.position + QcVec3{0.0, 0.3, 0.0});
        chat(u"/mob looking noai on"_s);
        QVERIFY(mob(chickenId)->noAi);
        chat(u"/mob 999999 noai off"_s);
        chat(u"/mob nearest noai maybe"_s);
        QVERIFY(std::ranges::all_of(m_server->entities().creatures().mobs(), &QcCreature::noAi));
    }
};

QTEST_GUILESS_MAIN(TestCreatureMinds)
#include "test_creature_minds.moc"
