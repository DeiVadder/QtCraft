#include "common/content/parser/QcContentParser.h"
#include "common/world/QcDayCycle.h"
#include "QcServerTestFixture.h"
#include "server/golems/QcGolemGrudges.h"
#include "server/storage/QcEntityCodec.h"
#include "server/villages/QcVillage.h"
#include "server/villages/QcVillages.h"

#include <QJsonDocument>

using namespace Qt::StringLiterals;

// The iron golem on a server (decision E-471), with ticks stepped by hand: its data and the rules
// for golem kinds; it hunts hostile creatures (endermen too, without provoking them at a player)
// and throws them upwards but leaves players alone, unless they hurt it or the villagers or golems
// of the village it stands in (never when a player built it); players build it from four iron blocks in a T and a carved pumpkin in all four
// directions, and wrong shapes stay blocks; shears carve pumpkins (decision E-472); an iron ingot
// repairs it; it drops iron and poppies; whether a player built it survives saving.
class TestIronGolems : public QcServerTestFixture
{
    Q_OBJECT

private:
    static constexpr double midnight = QcDayCycle::dayLength * 10.75;
    // Half the edge of the flat arena around the player.
    static constexpr int arena = 20;

    QcServerClient &player() { return *m_server->entities().clients().front(); }
    int type(const char *name) const { return *m_content->mobIndex(u"qtcraft:"_s + QString::fromLatin1(name)); }
    QcCreature *mob(quint32 id) { return m_server->entities().creatures().find(id); }
    const QcMobDefinition &golemKind() const { return m_content->mobs()[type("iron_golem")]; }

    // A server at night without creatures spawning by themselves, with a flat, open arena of grass
    // around the player.
    void joinArena()
    {
        join();
        chat(u"/gamerule mobSpawning false"_s);
        m_server->setWorldTime(midnight);
        loadAround(m_feet, 2);
        for (int dx = -arena; dx <= arena; ++dx) {
            for (int dz = -arena; dz <= arena; ++dz) {
                m_server->world().setBlock(m_feet.offset(dx, -1, dz), id("qtcraft:grass_block"));
                for (int dy = 0; dy <= 8; ++dy) {
                    m_server->world().setBlock(m_feet.offset(dx, dy, dz), qcAirBlockId);
                }
            }
        }
        m_server->entities().creatures().removeAll();
    }

    quint32 spawn(const char *name, int dx, int dz)
    {
        const QcVec3 at{m_feet.x + dx + 0.5, double(m_feet.y), m_feet.z + dz + 0.5};
        return m_server->entities().creatures().spawn(type(name), at, m_server->entities().newId(), false, true);
    }

    qsizetype golems() const { return std::ranges::count(m_server->entities().creatures().mobs(), type("iron_golem"), &QcCreature::type); }

    // Gives the player `count` of an item and puts it in the hand.
    void hold(const char *name, int count = 1)
    {
        chat(u"/give %1 %2"_s.arg(QString::fromLatin1(name)).arg(count));
        ticks(1);
        send(QcMsg::SelectHotbarSlot{slotOf(item((u"qtcraft:"_s + QString::fromLatin1(name)).toUtf8().constData()))});
    }

    // How many of an item the player has.
    int held(const char *name)
    {
        const QcItemId wanted = item((u"qtcraft:"_s + QString::fromLatin1(name)).toUtf8().constData());
        int count = 0;
        for (int slot = 0; slot < QcInventory::slotCount; ++slot) {
            count += player().inventory.slot(slot).item == wanted ? player().inventory.slot(slot).count : 0;
        }
        return count;
    }

    // A T of `body` blocks three blocks north of the player, its foot on the floor, the arms along x
    // (`alongX`) or z, with a support block beside the head cell to place the head against; returns
    // the head cell and the face of the support to click.
    std::pair<QcBlockPos, QcBlockFace> buildBody(bool alongX, const char *body = "qtcraft:iron_block")
    {
        const QcBlockPos foot = m_feet.offset(0, 0, -3);
        const QcBlockPos middle = foot.offset(0, 1, 0);
        const QcBlockPos head = foot.offset(0, 2, 0);
        const int dx = alongX ? 1 : 0;
        const int dz = alongX ? 0 : 1;
        for (const QcBlockPos cell : {foot, middle, middle.offset(dx, 0, dz), middle.offset(-dx, 0, -dz)}) {
            m_server->world().setBlock(cell, id(body));
        }
        // Behind the head (north) for a T along x, east of it for one along z: not beside it.
        const QcBlockPos support = alongX ? head.offset(0, 0, -1) : head.offset(1, 0, 0);
        m_server->world().setBlock(support, id("qtcraft:stone"));
        return {head, alongX ? QcBlockFace::South : QcBlockFace::West};
    }

    // Places the held block against the support beside `head` (buildBody) with the player looking
    // towards `yaw`.
    void placeHead(std::pair<QcBlockPos, QcBlockFace> head, float yaw)
    {
        const QcFaceOffset offset = qcFaceOffset(head.second);
        const QcBlockPos support{head.first.x - offset.dx, head.first.y - offset.dy, head.first.z - offset.dz};
        player().yaw = yaw;
        send(QcMsg::UseItem{support, head.second, {0.5, 0.5, 0.5}});
    }

    // Clears the cells around the build site again.
    void clearSite()
    {
        for (int dx = -2; dx <= 2; ++dx) {
            for (int dy = 0; dy <= 3; ++dy) {
                for (int dz = -5; dz <= -1; ++dz) {
                    m_server->world().setBlock(m_feet.offset(dx, dy, dz), qcAirBlockId);
                }
            }
        }
        m_server->entities().creatures().removeAll();
    }

    // Ticks until `done` holds, at most `limit` ticks; whether it held.
    bool tickUntil(const std::function<bool()> &done, int limit)
    {
        for (int tick = 0; tick < limit; ++tick) {
            if (done()) {
                return true;
            }
            ticks(1);
        }
        return done();
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void golemsComeFromData()
    {
        const QcMobDefinition &golem = golemKind();
        QCOMPARE(golem.ai, QcMobDefinition::Ai::Golem);
        QVERIFY(!golem.hostile && golem.maxHealth >= 100.0 && golem.width > 1.0 && golem.height > 2.5);
        QVERIFY(golem.walkSpeed < m_content->mobs()[type("villager")].walkSpeed);
        QCOMPARE(golem.golem.knockbackResistance, 1.0);
        QVERIFY(golem.golem.lift > 1.0);
        QCOMPARE(golem.golem.repairItem, u"qtcraft:iron_ingot"_s);
        QCOMPARE(golem.golem.buildBody, u"qtcraft:iron_block"_s);
        QCOMPARE(golem.golem.buildHead, u"qtcraft:carved_pumpkin"_s);
        QVERIFY(!golem.spawnsNaturally());
        // Iron ingots (3 to 5) and poppies (0 to 2).
        int iron = 0;
        int poppies = 0;
        for (const QcItemDrop &drop : golem.drops) {
            iron += drop.item == u"qtcraft:iron_ingot"_s ? drop.count : 0;
            poppies += drop.item == u"qtcraft:poppy"_s ? drop.count : 0;
        }
        QCOMPARE(iron, 5);
        QCOMPARE(poppies, 2);

        // The golem AI goes with golem settings, and only for peaceful walkers.
        const auto parses = [](const char *json) {
            return QcContentParser::parseMob(QJsonDocument::fromJson(json).object(), u"qtcraft"_s).has_value();
        };
        QVERIFY(parses(R"({"name":"g","skeleton":"humanoid","outfit":"zombie","ai":"golem","golem":{}})"));
        QVERIFY(!parses(R"({"name":"g","skeleton":"humanoid","outfit":"zombie","ai":"golem"})"));
        QVERIFY(!parses(R"({"name":"g","skeleton":"humanoid","outfit":"zombie","golem":{}})"));
        QVERIFY(!parses(R"({"name":"g","skeleton":"humanoid","outfit":"zombie","hostile":true,"ai":"golem","golem":{}})"));
        QVERIFY(!parses(R"({"name":"g","skeleton":"humanoid","outfit":"zombie","ai":"golem","golem":{"build":{"body":"a","head":"a"}}})"));
    }

    void aGolemHuntsHostilesAndThrowsThemUp()
    {
        joinArena();
        chat(u"/gamemode creative"_s);
        const quint32 golem = spawn("iron_golem", 0, -8);
        const quint32 zombie = spawn("zombie", 6, -8);
        const quint32 pig = spawn("pig", -4, -8);
        double highest = 0.0;
        const bool killed = tickUntil(
            [&] {
                if (const QcCreature *target = mob(zombie)) {
                    highest = std::max(highest, target->motion.velocity.y);
                }
                return mob(zombie) == nullptr;
            },
            400);
        QVERIFY(killed);
        // Thrown higher than a usual blow lifts.
        QVERIFY2(highest > QcMobManager::knockbackLift * 1.2, qPrintable(QString::number(highest)));
        QVERIFY(mob(golem) && mob(pig));
        QCOMPARE(mob(pig)->health, m_content->mobs()[type("pig")].maxHealth);

        // Blows do not push it back.
        const QcVec3 before = mob(golem)->motion.position;
        m_server->entities().creatures().damage({.creature = golem, .damage = 1.0, .from = before + QcVec3{1.0, 0.0, 0.0}});
        QCOMPARE(mob(golem)->motion.velocity.x, 0.0);
    }

    // Endermen count as hostile, so golems hunt them; a golem's blow comes from no player and so
    // provokes the enderman at nobody (decision E-460).
    void aGolemHuntsEndermenWithoutProvokingThemAtPlayers()
    {
        joinArena();
        // Looking straight up: no look at the enderman's head.
        player().pitch = 90.0f;
        const double full = player().health;
        const quint32 golem = spawn("iron_golem", 0, -8);
        const quint32 enderman = spawn("enderman", 6, -8);
        // Kept in place, so that it does not teleport out of the golem's reach.
        mob(enderman)->confined = true;
        const double fullEnderman = mob(enderman)->health;
        bool provoked = false;
        double lowest = fullEnderman;
        tickUntil(
            [&] {
                if (const QcCreature *target = mob(enderman)) {
                    provoked = provoked || target->provokedBy != 0 || target->struckBy != 0;
                    lowest = std::min(lowest, target->health);
                    return false;
                }
                lowest = 0.0;
                return true;
            },
            600);
        QVERIFY(lowest < fullEnderman);
        QVERIFY(!provoked);
        QVERIFY(mob(golem));
        QCOMPARE(player().health, full);
    }

    void aGolemLeavesPlayersAloneUntilTheyHurtIt()
    {
        joinArena();
        const quint32 golem = spawn("iron_golem", 0, -2);
        const double full = player().health;
        ticks(100);
        QCOMPARE(player().health, full);

        // Hurt by the player: it strikes back and throws them up.
        send(QcMsg::AttackEntity{golem});
        QCOMPARE(mob(golem)->golem->angryAt, player().id);
        double highest = 0.0;
        QVERIFY(tickUntil(
            [&] {
                highest = std::max(highest, player().motion.velocity.y);
                return player().health < full;
            },
            200));
        QVERIFY(highest > QcMobManager::knockbackLift);

        // Its anger passes.
        chat(u"/gamemode creative"_s);
        chat(u"/gamemode survival"_s);
        m_server->entities().creatures().find(golem)->golem->angryUntil = m_server->worldTime();
        chat(u"/heal"_s);
        const double healed = player().health;
        ticks(100);
        QCOMPARE(player().health, healed);
    }

    void aGolemAPlayerBuiltNeverAttacksPlayers()
    {
        joinArena();
        const quint32 golem = spawn("iron_golem", 0, -2);
        mob(golem)->golem->playerBuilt = true;
        const double full = player().health;
        send(QcMsg::AttackEntity{golem});
        QCOMPARE(mob(golem)->golem->angryAt, 0U);
        ticks(150);
        QCOMPARE(player().health, full);
    }

    void aVillageHoldsAGrudgeAgainstWhoHurtsItsVillagers()
    {
        joinArena();
        // A village around the arena with a villager of it beside the player.
        QcVillage village;
        village.id = 9;
        village.style = u"qtcraft:plains"_s;
        village.centre = m_feet;
        village.minX = m_feet.x - 10;
        village.maxX = m_feet.x + 10;
        village.minY = m_feet.y - 5;
        village.maxY = m_feet.y + 10;
        village.minZ = m_feet.z - 10;
        village.maxZ = m_feet.z + 10;
        m_server->world().villages().restore({village});
        const quint32 villager = spawn("villager", 1, -2);
        mob(villager)->villager->village = 9;
        const quint32 guard = spawn("iron_golem", -8, -8);
        const quint32 built = spawn("iron_golem", 8, -8);
        mob(built)->golem->playerBuilt = true;
        const double full = player().health;

        send(QcMsg::AttackEntity{villager});
        const QcVillage *state = m_server->world().villages().find(9);
        QVERIFY(state->grudges.until.value(player().name, -1.0) > m_server->worldTime());
        // Held by name and passing: a village equals itself without its grudges.
        QcVillage forgiven = *state;
        forgiven.grudges.until.clear();
        QVERIFY(forgiven == *state);
        // The guard, not hurt itself, comes for the player.
        QVERIFY(tickUntil([&] { return player().health < full; }, 300));
        QCOMPARE(mob(guard)->golem->angryAt, 0U);

        // The grudge ends: the guard leaves the player alone; the golem a player built never came.
        m_server->world().villages().find(9)->grudges.until.clear();
        chat(u"/heal"_s);
        const double healed = player().health;
        ticks(150);
        QCOMPARE(player().health, healed);
        QVERIFY(mob(built));
    }

    // A village golem strolls within its village and walks back into it from outside (decision
    // E-471).
    void aVillageGolemKeepsToItsVillage()
    {
        joinArena();
        QcVillage village;
        village.id = 9;
        village.style = u"qtcraft:plains"_s;
        village.centre = m_feet;
        village.minX = m_feet.x;
        village.maxX = m_feet.x;
        village.minY = m_feet.y - 5;
        village.maxY = m_feet.y + 10;
        village.minZ = m_feet.z;
        village.maxZ = m_feet.z;
        m_server->world().villages().restore({village});
        // Its area reaches `edge` blocks from the centre, within the arena.
        const int edge = QcVillage::reach;
        const quint32 guard = spawn("iron_golem", edge + 3, 0);
        QVERIFY(!village.contains(QcPhysics::blockContaining(mob(guard)->motion.position)));

        QVERIFY(tickUntil([&] { return village.contains(QcPhysics::blockContaining(mob(guard)->motion.position)); }, 600));
        for (int i = 0; i < 600; ++i) {
            ticks(1);
            QVERIFY2(std::abs(mob(guard)->motion.position.x - (m_feet.x + 0.5)) <= edge + 2.0, qPrintable(QString::number(i)));
            QVERIFY(std::abs(mob(guard)->motion.position.z - (m_feet.z + 0.5)) <= edge + 2.0);
        }
    }

    void playersBuildAGolemInEveryDirection_data()
    {
        QTest::addColumn<bool>("alongX");
        QTest::addColumn<float>("yaw");
        QTest::newRow("along x, facing south") << true << 0.0F;
        QTest::newRow("along x, facing north") << true << 180.0F;
        QTest::newRow("along z, facing east") << false << 270.0F;
        QTest::newRow("along z, facing west") << false << 90.0F;
    }

    void playersBuildAGolemInEveryDirection()
    {
        QFETCH(bool, alongX);
        QFETCH(float, yaw);
        joinArena();
        hold("carved_pumpkin");
        const auto head = buildBody(alongX);
        placeHead(head, yaw);

        // The blocks became the golem, standing where the foot was.
        QCOMPARE(golems(), 1);
        const QcCreature &golem = *std::ranges::find(m_server->entities().creatures().mobs(), type("iron_golem"), &QcCreature::type);
        QVERIFY(golem.golem && golem.golem->playerBuilt && golem.persistent);
        const QcBlockPos foot = head.first.offset(0, -2, 0);
        // (It may have stepped aside from the support block, which its wide body touches.)
        QVERIFY(std::hypot(golem.motion.position.x - (foot.x + 0.5), golem.motion.position.z - (foot.z + 0.5)) <= 1.5);
        QVERIFY(std::abs(golem.motion.position.y - foot.y) <= 1.0);
        for (const QcBlockPos cell : {head.first, foot, foot.offset(0, 1, 0), foot.offset(alongX ? 1 : 0, 1, alongX ? 0 : 1),
                                      foot.offset(alongX ? -1 : 0, 1, alongX ? 0 : -1)}) {
            QCOMPARE(m_server->world().blockAt(cell), std::optional<QcBlockId>(qcAirBlockId));
        }
        // The pumpkin is used up (survival).
        QCOMPARE(held("carved_pumpkin"), 0);
    }

    void wrongShapesStayBlocks()
    {
        joinArena();
        hold("carved_pumpkin", 16);
        const auto attempt = [&](const std::function<void()> &spoil) {
            clearSite();
            const auto head = buildBody(true);
            spoil();
            placeHead(head, 0.0F);
            return std::pair{golems(), m_server->world().blockAt(head.first)};
        };
        const QcBlockPos middle = m_feet.offset(0, 1, -3);

        // One arm missing; a stone body; dirt beside the foot; snow beside the head (a T needs air there).
        QCOMPARE(attempt([&] { m_server->world().setBlock(middle.offset(1, 0, 0), qcAirBlockId); }).first, 0);
        QCOMPARE(attempt([&] { m_server->world().setBlock(middle.offset(0, -1, 0), id("qtcraft:stone")); }).first, 0);
        QCOMPARE(attempt([&] { m_server->world().setBlock(middle.offset(1, -1, 0), id("qtcraft:dirt")); }).first, 0);
        const auto beside = attempt([&] { m_server->world().setBlock(middle.offset(-1, 1, 0), id("qtcraft:stone")); });
        QCOMPARE(beside.first, 0);
        QVERIFY(beside.second && m_content->block(*beside.second).name == u"qtcraft:carved_pumpkin"_s);
        // A plain pumpkin is no head.
        hold("pumpkin");
        QCOMPARE(attempt([] {}).first, 0);
        // The complete T with the carved pumpkin: one golem.
        hold("carved_pumpkin");
        QCOMPARE(attempt([] {}).first, 1);
    }

    void shearsCarveAPumpkin()
    {
        joinArena();
        const QcBlockPos pumpkin = m_feet.offset(0, 0, -2);
        m_server->world().setBlock(pumpkin, id("qtcraft:pumpkin"));
        hold("shears");
        send(QcMsg::UseItem{pumpkin, QcBlockFace::South, {0.5, 0.5, 1.0}});
        const std::optional<QcBlockId> carved = m_server->world().blockAt(pumpkin);
        QVERIFY(carved);
        QCOMPARE(m_content->block(*carved).name, u"qtcraft:carved_pumpkin"_s);
        QCOMPARE(m_content->property(*carved, u"facing"_s), u"south"_s);
        int seeds = 0;
        for (const QcItemEntity &drop : m_server->entities().items().items()) {
            seeds += drop.stack.item == item("qtcraft:pumpkin_seeds") ? drop.stack.count : 0;
        }
        QCOMPARE(seeds, 4);
        QCOMPARE(player().inventory.slot(player().inventory.selectedHotbarSlot()).damage, 1);

        // A carved pumpkin is not carved again.
        send(QcMsg::UseItem{pumpkin, QcBlockFace::East, {1.0, 0.5, 0.5}});
        QCOMPARE(m_content->property(*m_server->world().blockAt(pumpkin), u"facing"_s), u"south"_s);
    }

    void anIronIngotRepairsAGolem()
    {
        joinArena();
        const quint32 golem = spawn("iron_golem", 0, -2);
        mob(golem)->golem->playerBuilt = true;
        mob(golem)->health = 40.0;
        hold("iron_ingot", 3);
        send(QcMsg::UseEntity{golem});
        QCOMPARE(mob(golem)->health, 40.0 + golemKind().golem.repairHealth);
        QCOMPARE(held("iron_ingot"), 2);
        mob(golem)->health = golemKind().maxHealth - 5.0;
        send(QcMsg::UseEntity{golem});
        QCOMPARE(mob(golem)->health, golemKind().maxHealth);
        // At full health it takes nothing.
        send(QcMsg::UseEntity{golem});
        QCOMPARE(held("iron_ingot"), 1);
    }

    // A golem is no animal (decision E-470): one beside the player does not keep animals from
    // spawning around them.
    void aGolemTakesNoRoomFromAnimals()
    {
        joinArena();
        chat(u"/gamerule mobSpawning true"_s);
        m_server->setWorldTime(QcDayCycle::dayLength * 10.25);
        spawn("iron_golem", 3, 0);
        const auto animals = [&] {
            return std::ranges::count_if(m_server->entities().creatures().mobs(), [&](const QcCreature &mob) { return mob.type != type("iron_golem"); });
        };
        for (int i = 0; i < 1200 && animals() == 0; ++i) {
            ticks(1);
        }
        QVERIFY(animals() > 0);
    }

    void whetherAPlayerBuiltItIsSaved()
    {
        QcEntityRecord record;
        record.chunk = {0, 0};
        QcMobManager::Stored built{type("iron_golem"), {8.5, 65.0, 8.5}, 0.0f, 60.0, 0.0, 0.0, false, std::nullopt, 12};
        built.playerBuilt = true;
        QcMobManager::Stored village = built;
        village.uid = 13;
        village.playerBuilt = false;
        record.creatures = {{built, 0}, {village, 1}};
        QStringList warnings;
        QSet<QString> reported;
        const QcResult<QcEntityRecord> decoded =
            QcEntityCodec::decode(QcEntityCodec::encode(record, *m_content), {0, 0}, *m_content, warnings, reported);
        QVERIFY(decoded && decoded->creatures.size() == 2);
        QVERIFY(decoded->creatures[0].entry.playerBuilt);
        QVERIFY(!decoded->creatures[1].entry.playerBuilt);

        // Restored, the creature carries it again.
        QcMobManager mobs(m_content, 1);
        quint32 next = 1;
        mobs.restore({decoded->creatures[0].entry, decoded->creatures[1].entry}, {}, [&next] { return next++; });
        QVERIFY(mobs.mobs()[0].golem && mobs.mobs()[0].golem->playerBuilt);
        QVERIFY(mobs.mobs()[1].golem && !mobs.mobs()[1].golem->playerBuilt);
        QCOMPARE(QcMobManager::storedOf(mobs.mobs()[0]).playerBuilt, true);
    }
};

QTEST_GUILESS_MAIN(TestIronGolems)
#include "test_iron_golems.moc"
