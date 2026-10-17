#include "common/content/definitions/QcSoundKeys.h"
#include "common/gameplay/QcExperience.h"
#include "common/world/QcDayCycle.h"
#include "QcServerTestFixture.h"
#include "server/gameplay/QcEnvironmentDamage.h"
#include "server/items/QcExperienceOrbManager.h"
#include "server/villages/QcVillagerHomes.h"
#include "server/villages/QcVillagerTrades.h"
#include "worldgen/villages/QcVillageGenerator.h"

#include <QTemporaryDir>

using namespace Qt::StringLiterals;

namespace {

// The test terrain with one village (id 77) around the spawn and nothing built: the tests place the
// bed and the workstation.
class TradingTerrain : public QcTestTerrainGenerator
{
public:
    explicit TradingTerrain(std::shared_ptr<const QcContentRegistry> content)
        : QcTestTerrainGenerator(std::move(content))
    {
        auto plan = std::make_shared<QcVillagePlan>();
        const QcBlockPos spawn = spawnPoint();
        plan->id = 77;
        plan->style = u"qtcraft:plains"_s;
        plan->layout = u"cross"_s;
        plan->centre = spawn;
        plan->minX = spawn.x - 24;
        plan->maxX = spawn.x + 24;
        plan->minZ = spawn.z - 24;
        plan->maxZ = spawn.z + 24;
        m_plan = std::move(plan);
    }

    std::shared_ptr<const QcVillagePlan> villageFor(QcChunkPos) const override { return m_plan; }
    std::shared_ptr<const QcVillagePlan> nearestVillage(QcBlockPos, int) const override { return m_plan; }

private:
    std::shared_ptr<QcVillagePlan> m_plan;
};

} // namespace

// Trading with villagers on a server (decision E-414): opening the screen, paying and getting the
// result, uses and restocking at the workstation, levels unlocking offers, experience for the
// player, the screen closing on danger and distance, one trader per villager; children do not
// trade (decision E-415). An awake villager with its head in a wall suffocates.
class TestTrading : public QcServerTestFixture
{
    Q_OBJECT

private:
    // Work time of the routine; at 0.27 the villagers meet (no restock).
    static constexpr double workPhase = 0.1;
    static constexpr double meetPhase = 0.27;

    QcVillage *village() { return m_server->world().villages().find(77); }

    void setPhase(double phase) { m_server->setWorldTime(QcDayCycle::dayLength * (10.0 + phase)); }

    // A server with the village and a farmer living there, its composter claimed: the time stands at
    // `phase`, no creatures spawn by themselves. Returns the farmer.
    quint32 startWithFarmer(double phase, const QcServerConfig &config = {})
    {
        join(config, std::make_unique<TradingTerrain>(m_content));
        chat(u"/gamerule daylightCycle false"_s);
        chat(u"/gamerule mobSpawning false"_s);
        chat(u"/gamerule weatherCycle false"_s);
        loadAround(m_feet, 2);
        for (int dx = -8; dx <= 8; ++dx) {
            for (int dz = -8; dz <= 8; ++dz) {
                m_server->world().setBlock(m_feet.offset(dx, -1, dz), id("qtcraft:stone"));
                for (int dy = 0; dy <= 3; ++dy) {
                    m_server->world().setBlock(m_feet.offset(dx, dy, dz), qcAirBlockId);
                }
            }
        }
        setPhase(phase);

        const QcBlockPos foot = m_feet.offset(4, 0, 2);
        m_server->world().setBlock(foot, *m_content->stateId(u"qtcraft:bed[facing=north,part=foot]"_s));
        m_server->world().setBlock(foot.offset(0, 0, -1), *m_content->stateId(u"qtcraft:bed[facing=north,part=head]"_s));
        m_server->world().setBlock(m_feet.offset(2, 0, 2), id("qtcraft:composter"));

        const quint32 entity = m_server->entities().newId();
        m_server->mobs().spawn(*m_content->mobIndex(u"qtcraft:villager"_s), QcVec3{m_feet.x + 2.5, double(m_feet.y), m_feet.z + 1.5}, entity);
        villager(entity)->villager = QcVillagerHomes::settle(*village(), foot.offset(0, 0, -1), villager(entity)->uid, m_content->villagerSettings(), 7);
        ticks(30);
        return entity;
    }

    QcCreature *villager(quint32 entity) { return m_server->mobs().find(entity); }

    const QcVillagerTrades &trades() const { return m_server->mobs().villagers().trades(); }

    // Holds the villager next to the player (the routine would walk it away between ticks), or
    // `dx` blocks east of the player.
    void holdNear(quint32 entity, int dx = 1)
    {
        villager(entity)->motion.position = QcVec3{m_feet.x + dx + 0.5, double(m_feet.y), m_feet.z + 0.5};
        villager(entity)->motion.velocity = {};
    }

    // The player's input with `sneak`.
    void sneak(bool sneaking)
    {
        QcPlayerInput input;
        input.sequence = ++m_sequence;
        input.sneak = sneaking;
        m_client->connection.send(QcMsg::PlayerInput{input});
        pump();
        ticks(1);
    }

    int countOf(const char *name) const
    {
        int count = 0;
        for (const QcItemStack &stack : std::as_const(m_client->stacks)) {
            count += stack.item == item(name) ? stack.count : 0;
        }
        return count;
    }

    // The index of the open screen's offer paying `cost` for `result`, -1 without one.
    int offerIndex(const char *cost, const char *result) const
    {
        for (qsizetype i = 0; m_client->trade && i < m_client->trade->offers.size(); ++i) {
            const QcMsg::TradeOffer &offer = m_client->trade->offers[i];
            if (offer.costs.constFirst().item == item(cost) && offer.result.item == item(result)) {
                return int(i);
            }
        }
        return -1;
    }

    // Opens the farmer's screen with `wheat` wheat in the inventory; the index of its wheat offer.
    int openWithWheat(quint32 farmer, int wheat)
    {
        if (wheat > 0) {
            chat(u"/give wheat %1"_s.arg(wheat));
        }
        holdNear(farmer);
        send(QcMsg::UseEntity{farmer});
        ticks(1);
        return offerIndex("qtcraft:wheat", "qtcraft:emerald");
    }

    // A farmer whose look draws the wheat offer at level 1.
    quint32 startWithWheatFarmer(double phase, const QcServerConfig &config = {})
    {
        const quint32 farmer = startWithFarmer(phase, config);
        for (quint32 look = 0; look < 64; ++look) {
            villager(farmer)->villager.look = look;
            const QList<const QcVillagerTrades::Offer *> offers = trades().offersOf(villager(farmer)->villager);
            if (std::ranges::any_of(offers, [](const auto *offer) { return offer->definition.id == u"qtcraft:wheat>qtcraft:emerald"_s; })) {
                return farmer;
            }
        }
        qFatal("No look draws the wheat offer");
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void aFarmerOpensItsScreenWithItsOffers()
    {
        const quint32 farmer = startWithWheatFarmer(meetPhase);
        QCOMPARE(villager(farmer)->villager.profession, u"qtcraft:farmer"_s);
        QVERIFY(openWithWheat(farmer, 0) >= 0);
        QCOMPARE(m_client->trade->villager, farmer);
        QCOMPARE(m_client->trade->profession, u"profession.qtcraft.farmer"_s);
        QCOMPARE(m_client->trade->level, 1);
        QCOMPARE(m_client->trade->levelEnd, 10);
        QCOMPARE(m_client->trade->offers.size(), trades().settings().offersPerLevel);
        QCOMPARE(villager(farmer)->villager.trader, m_client->entityId);

        // It stands facing the player while the screen is open.
        ticks(40);
        QVERIFY(m_client->trade.has_value());
        QVERIFY(!villager(farmer)->villager.target);
    }

    void aTradePaysTheCostsAndGivesTheResult()
    {
        const quint32 farmer = startWithWheatFarmer(meetPhase);
        const int index = openWithWheat(farmer, 30);
        QVERIFY(index >= 0);

        send(QcMsg::Trade{farmer, index, false});
        ticks(1);
        QCOMPARE(countOf("qtcraft:wheat"), 10);
        QCOMPARE(countOf("qtcraft:emerald"), 1);
        QCOMPARE(m_client->trade->offers[index].uses, 1);
        QCOMPARE(villager(farmer)->villager.experience, 2);

        // Ten wheat are not enough: nothing changes.
        send(QcMsg::Trade{farmer, index, false});
        ticks(1);
        QCOMPARE(countOf("qtcraft:wheat"), 10);
        QCOMPARE(countOf("qtcraft:emerald"), 1);
        QCOMPARE(m_client->trade->offers[index].uses, 1);

        // Wrong villager or offer: ignored.
        chat(u"/give wheat 20"_s);
        send(QcMsg::Trade{farmer + 1, index, false});
        send(QcMsg::Trade{farmer, 99, false});
        ticks(1);
        QCOMPARE(countOf("qtcraft:wheat"), 30);
    }

    void thePlayerGetsExperienceOrbs()
    {
        const quint32 farmer = startWithWheatFarmer(meetPhase);
        const int index = openWithWheat(farmer, 20);
        const int before = m_client->experience ? m_client->experience->points : 0;
        send(QcMsg::Trade{farmer, index, false});
        ticks(1);
        QVERIFY(!m_server->entities().orbs().orbs().isEmpty());
        ticks(40);
        QVERIFY(m_client->experience);
        QVERIFY(m_client->experience->points > before || m_client->experience->level > 0);
    }

    void aRepeatedTradeGoesOnWhileItCan()
    {
        const quint32 farmer = startWithWheatFarmer(meetPhase);
        const int index = openWithWheat(farmer, 64);
        send(QcMsg::Trade{farmer, index, true});
        ticks(1);
        QCOMPARE(countOf("qtcraft:emerald"), 3);
        QCOMPARE(countOf("qtcraft:wheat"), 4);
        QCOMPARE(m_client->trade->offers[index].uses, 3);
    }

    void aSoldOutOfferWaitsForTheRestockAtWork()
    {
        const quint32 farmer = startWithWheatFarmer(meetPhase);
        const int index = openWithWheat(farmer, 0);
        const int maxUses = m_client->trade->offers[index].maxUses;
        chat(u"/give wheat %1"_s.arg(20 * (maxUses + 1)));
        send(QcMsg::Trade{farmer, index, true});
        ticks(1);
        QCOMPARE(m_client->trade->offers[index].uses, maxUses);
        QCOMPARE(countOf("qtcraft:wheat"), 20);
        send(QcMsg::Trade{farmer, index, false});
        ticks(1);
        QCOMPARE(countOf("qtcraft:wheat"), 20);

        // Back at work, the farmer restocks; the sold-out offer costs more now (demand).
        send(QcMsg::CloseTrade{});
        setPhase(workPhase);
        bool restocked = false;
        for (int tick = 0; tick < 400 && !restocked; ++tick) {
            ticks(1);
            restocked = villager(farmer)->villager.offers.value(index).uses == 0;
        }
        QVERIFY(restocked);
        QCOMPARE(villager(farmer)->villager.restocks, 1);
        QCOMPARE(villager(farmer)->villager.offers[index].demand, 1);
        openWithWheat(farmer, 0);
        QCOMPARE(m_client->trade->offers[index].costs.constFirst().count, 22);
    }

    void aLevelReachedUnlocksOffers()
    {
        const quint32 farmer = startWithWheatFarmer(meetPhase);
        villager(farmer)->villager.experience = 9;
        const int index = openWithWheat(farmer, 20);
        const qsizetype before = m_client->trade->offers.size();
        send(QcMsg::Trade{farmer, index, false});
        ticks(1);
        QCOMPARE(m_client->trade->level, 2);
        QCOMPARE(m_client->trade->levelStart, 10);
        QCOMPARE(m_client->trade->levelEnd, 70);
        QCOMPARE(m_client->trade->offers.size(), before + trades().settings().offersPerLevel);
        QVERIFY(m_client->sounds.contains(QString::fromLatin1(QcSoundKeys::levelUp)));
    }

    void theScreenClosesWhenDangerComes()
    {
        const quint32 farmer = startWithWheatFarmer(meetPhase);
        QVERIFY(openWithWheat(farmer, 0) >= 0);
        m_server->mobs().spawn(*m_content->mobIndex(u"qtcraft:zombie"_s), villager(farmer)->motion.position + QcVec3{4.0, 0.0, 0.0},
                               m_server->entities().newId());
        for (int tick = 0; tick < 40 && !m_client->tradeClosed; ++tick) {
            ticks(1);
        }
        QVERIFY(m_client->tradeClosed);
        QCOMPARE(villager(farmer)->villager.trader, 0u);
        QVERIFY(villager(farmer)->fleeTimer > 0.0);
    }

    void theScreenClosesWhenThePlayerWalksAway()
    {
        const quint32 farmer = startWithWheatFarmer(meetPhase);
        QVERIFY(openWithWheat(farmer, 0) >= 0);
        chat(u"/tp %1 %2 %3"_s.arg(m_feet.x + 12).arg(m_feet.y).arg(m_feet.z));
        ticks(2);
        QVERIFY(m_client->tradeClosed);
        QCOMPARE(villager(farmer)->villager.trader, 0u);
    }

    void oneTraderAtATime()
    {
        const quint32 farmer = startWithWheatFarmer(meetPhase);
        QVERIFY(openWithWheat(farmer, 0) >= 0);

        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd));
        RawClient other(std::move(clientEnd));
        other.hello(u"Other"_s);
        pump();
        ticks(2);
        holdNear(farmer);
        other.connection.send(QcMsg::UseEntity{farmer});
        pump();
        ticks(1);
        QVERIFY(!other.trade);
        QCOMPARE(villager(farmer)->villager.trader, m_client->entityId);

        // Once the first closes, the second may trade.
        send(QcMsg::CloseTrade{});
        ticks(1);
        holdNear(farmer);
        other.connection.send(QcMsg::UseEntity{farmer});
        pump();
        ticks(1);
        QVERIFY(other.trade);
        QCOMPARE(villager(farmer)->villager.trader, other.entityId);

        // The trader leaves the game: the villager trades no more.
        other.connection.close();
        pump();
        ticks(3);
        QCOMPARE(villager(farmer)->villager.trader, 0u);
    }

    void aStateAfterTheProfessionCommandFollowsTheNewOffers()
    {
        const quint32 farmer = startWithWheatFarmer(meetPhase);
        QVERIFY(openWithWheat(farmer, 0) >= 0);
        chat(u"/profession smith 2"_s);
        ticks(1);
        QVERIFY(m_client->trade);
        QCOMPARE(m_client->trade->profession, u"profession.qtcraft.smith"_s);
        QCOMPARE(m_client->trade->offers.size(), 2 * trades().settings().offersPerLevel);
        QCOMPARE(villager(farmer)->villager.offers.size(), m_client->trade->offers.size());

        // A trade of the shown offer counts on that offer's state.
        const QcMsg::TradeOffer shown = m_client->trade->offers[0];
        for (const QcItemStack &cost : shown.costs) {
            chat(u"/give %1 %2"_s.arg(m_content->item(cost.item).name).arg(cost.count));
        }
        send(QcMsg::Trade{farmer, 0, false});
        ticks(1);
        QCOMPARE(m_client->trade->offers[0].uses, 1);
        const QString id = trades().offersOf(villager(farmer)->villager).constFirst()->definition.id;
        QCOMPARE(villager(farmer)->villager.offers[0].id, id);
        QCOMPARE(villager(farmer)->villager.offers[0].uses, 1);
    }

    void bedtimeEndsTheTradeAndNoneStartsAtNight()
    {
        const quint32 farmer = startWithWheatFarmer(meetPhase);
        QVERIFY(openWithWheat(farmer, 0) >= 0);
        setPhase(0.6);
        ticks(2);
        QVERIFY(m_client->tradeClosed);
        QCOMPARE(villager(farmer)->villager.trader, 0u);

        m_client->tradeClosed = false;
        QCOMPARE(openWithWheat(farmer, 0), -1);
        QVERIFY(!m_client->trade);
    }

    void noTradeThroughAWallOrWhileSneaking()
    {
        const quint32 farmer = startWithWheatFarmer(meetPhase);
        for (int dy = 0; dy <= 2; ++dy) {
            for (int dz = -1; dz <= 1; ++dz) {
                m_server->world().setBlock(m_feet.offset(2, dy, dz), id("qtcraft:stone"));
            }
        }
        holdNear(farmer, 3);
        send(QcMsg::UseEntity{farmer});
        QVERIFY(!m_client->trade);
        QCOMPARE(villager(farmer)->villager.trader, 0u);

        // Without the wall it trades; sneaking with bread in hand it does not (the bread is used).
        for (int dy = 0; dy <= 2; ++dy) {
            for (int dz = -1; dz <= 1; ++dz) {
                m_server->world().setBlock(m_feet.offset(2, dy, dz), qcAirBlockId);
            }
        }
        chat(u"/give bread 1"_s);
        sneak(true);
        holdNear(farmer, 3);
        send(QcMsg::UseEntity{farmer});
        QVERIFY(!m_client->trade);
        sneak(false);
        holdNear(farmer, 3);
        send(QcMsg::UseEntity{farmer});
        QVERIFY(m_client->trade);
    }

    void aRepeatedTradeDropsWhatDoesNotFitOnce()
    {
        // The inventory is full but for the wheat: the emeralds fall as one drop, the experience at once.
        const quint32 farmer = startWithWheatFarmer(meetPhase);
        const int index = openWithWheat(farmer, 64);
        chat(u"/give dirt %1"_s.arg(64 * (QcInventory::slotCount - 1)));
        ticks(1);
        QCOMPARE(countOf("qtcraft:wheat"), 64);
        const qsizetype itemsBefore = m_server->entities().items().items().size();
        const qsizetype orbsBefore = m_server->entities().orbs().orbs().size();
        send(QcMsg::Trade{farmer, index, true});
        QCOMPARE(countOf("qtcraft:wheat"), 4);
        QCOMPARE(countOf("qtcraft:emerald"), 0);
        QCOMPARE(m_client->trade->offers[index].uses, 3);

        const QList<QcItemEntity> &items = m_server->entities().items().items();
        QCOMPARE(items.size(), itemsBefore + 1);
        QCOMPARE(items.constLast().stack.item, item("qtcraft:emerald"));
        QCOMPARE(items.constLast().stack.count, 3);
        const QList<QcVillagerTrades::Offer> offers = trades().offersOfProfession(u"qtcraft:farmer"_s);
        const auto wheat = std::ranges::find(offers, u"qtcraft:wheat>qtcraft:emerald"_s, [](const auto &offer) { return offer.definition.id; });
        QVERIFY(wheat != offers.end());
        const int points = 3 * wheat->definition.playerExperience;
        int value = 0;
        for (const QcExperienceOrb &orb : m_server->entities().orbs().orbs()) {
            value += orb.value;
        }
        QCOMPARE(orbsBefore, 0);
        QCOMPARE(value, points);
        QCOMPARE(m_server->entities().orbs().orbs().size(), QcExperience::orbValues(points, QcExperienceOrbManager::maxOrbsPerDrop).size());
    }

    void aBlockScreenClosesTheTradingScreen()
    {
        const quint32 farmer = startWithWheatFarmer(meetPhase);
        QVERIFY(openWithWheat(farmer, 0) >= 0);
        m_server->world().setBlock(m_feet.offset(-1, 0, 0), id("qtcraft:furnace"));
        send(QcMsg::UseItem{m_feet.offset(-1, 0, 0), QcBlockFace::East, {1.0, 0.5, 0.5}});
        QVERIFY(m_client->openMachine);
        QVERIFY(m_client->tradeClosed);
        ticks(1);
        QCOMPARE(villager(farmer)->villager.trader, 0u);
    }

    void theScreenClosesWhenTheVillagerDiesOrIsUnloaded()
    {
        const quint32 farmer = startWithWheatFarmer(meetPhase);
        QVERIFY(openWithWheat(farmer, 0) >= 0);
        villager(farmer)->health = 0.0;
        ticks(2);
        QVERIFY(m_client->tradeClosed);

        // A villager whose chunk leaves the world is gone from the creatures.
        const quint32 other = startWithWheatFarmer(meetPhase);
        QVERIFY(openWithWheat(other, 0) >= 0);
        QVERIFY(m_server->mobs().remove(other));
        ticks(1);
        QVERIFY(m_client->tradeClosed);
    }

    void theScreenClosesWhenThePlayerDies()
    {
        const quint32 farmer = startWithWheatFarmer(meetPhase);
        QVERIFY(openWithWheat(farmer, 0) >= 0);
        chat(u"/kill"_s);
        ticks(2);
        QVERIFY(m_client->dead);
        QVERIFY(m_client->tradeClosed);
        QCOMPARE(villager(farmer)->villager.trader, 0u);
    }

    void aVillagerIsSavedWithItsOffers()
    {
        QTemporaryDir directory;
        QcServerConfig config;
        config.worldDirectory = directory.path();
        const quint32 farmer = startWithWheatFarmer(meetPhase, config);
        const int index = openWithWheat(farmer, 40);
        send(QcMsg::Trade{farmer, index, true});
        send(QcMsg::CloseTrade{});
        const QcVillagerRecord saved = static_cast<const QcVillagerRecord &>(villager(farmer)->villager);
        const quint64 uid = villager(farmer)->uid;
        QCOMPARE(saved.offers[index].uses, 2);
        QVERIFY(m_server->save().has_value());
        cleanup();

        join(config, std::make_unique<TradingTerrain>(m_content));
        loadAround(m_feet, 2);
        ticks(2);
        const auto found = std::ranges::find_if(m_server->mobs().mobs(), [uid](const QcCreature &mob) { return mob.uid == uid; });
        QVERIFY(found != m_server->mobs().mobs().end());
        QCOMPARE(found->villager.offers, saved.offers);
        QCOMPARE(found->villager.look, saved.look);
        QCOMPARE(trades().offersOf(found->villager), trades().offersOf(saved));
    }

    void aChildDoesNotTrade()
    {
        // Young with the farmer's offers all the same (a child is unemployed): it only looks up.
        const quint32 farmer = startWithWheatFarmer(meetPhase);
        villager(farmer)->growUp = m_content->villagerSettings().breeding.growUpSeconds;
        QCOMPARE(openWithWheat(farmer, 0), -1);
        QVERIFY(!m_client->trade);
        QCOMPARE(villager(farmer)->villager.trader, 0u);

        // Grown up it trades.
        villager(farmer)->growUp = 0.0;
        QVERIFY(openWithWheat(farmer, 0) >= 0);
    }

    void anAwakeVillagerSuffocatesWithItsHeadInAWall()
    {
        // Shut in stone with only its feet's cell free, standing: the eyes are in the stone above
        // (only a villager asleep in its bed has them low, E-415).
        const quint32 farmer = startWithFarmer(meetPhase);
        const QcBlockPos feet = m_feet.offset(-4, 0, -4);
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 2; ++dy) {
                for (int dz = -1; dz <= 1; ++dz) {
                    m_server->world().setBlock(feet.offset(dx, dy, dz), dx == 0 && dy == 0 && dz == 0 ? qcAirBlockId : id("qtcraft:stone"));
                }
            }
        }
        villager(farmer)->motion.position = QcVec3{feet.x + 0.5, double(feet.y), feet.z + 0.5};
        villager(farmer)->motion.velocity = {};
        const double before = villager(farmer)->health;
        ticks(3 * QcEnvironmentDamage::suffocationIntervalTicks);
        QVERIFY(!villager(farmer)->villager.sleeping);
        QVERIFY2(villager(farmer)->health < before, qPrintable(QString::number(villager(farmer)->health)));
    }
};

QTEST_GUILESS_MAIN(TestTrading)
#include "test_trading.moc"
