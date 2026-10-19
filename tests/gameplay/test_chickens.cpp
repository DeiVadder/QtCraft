#include "QcServerTestFixture.h"
#include "server/mobs/QcMobLaying.h"
#include "server/projectiles/QcHitEffects.h"
#include "server/storage/QcEntityCodec.h"

using namespace Qt::StringLiterals;

// Chickens and eggs (decision E-452): slow falling, laying on a timer that is saved, the seeded chance
// of an egg's chicks, breeding with seeds, their drops and the zombie without feathers. Ticks are
// stepped by hand.
class TestChickens : public QcServerTestFixture
{
    Q_OBJECT

private:
    int chickenType() const { return *m_content->mobIndex(u"qtcraft:chicken"_s); }
    QcCreature *mob(quint32 id) { return m_server->entities().creatures().find(id); }
    const QcMobDefinition &chicken() const { return m_content->mobs()[chickenType()]; }

    quint32 spawn(int dx, int dy, int dz)
    {
        const QcVec3 at{m_feet.x + dx + 0.5, double(m_feet.y + dy), m_feet.z + dz + 0.5};
        return m_server->entities().creatures().spawn(chickenType(), at, m_server->entities().newId(), false, true);
    }

    qsizetype chickens() const
    {
        return std::ranges::count(m_server->entities().creatures().mobs(), chickenType(), &QcCreature::type);
    }

    void hold(const char *name)
    {
        const int slot = slotOf(item(name));
        QVERIFY2(slot >= 0 && slot < 9, name);
        send(QcMsg::SelectHotbarSlot{slot});
    }

private slots:
    void chickensComeFromData()
    {
        QCOMPARE(chicken().maxFallSpeed, 2.5);
        QCOMPARE(chicken().laying.item, u"qtcraft:egg"_s);
        QCOMPARE(chicken().laying.minSeconds, 300.0);
        QCOMPARE(chicken().laying.maxSeconds, 600.0);
        QVERIFY(chicken().breeding.food.contains(u"qtcraft:wheat_seeds"_s));
        QVERIFY(chicken().breeding.food.contains(u"qtcraft:pumpkin_seeds"_s));
        // Feathers 0 to 2 and one raw chicken.
        const auto feathers = std::ranges::count(chicken().drops, u"qtcraft:feather"_s, &QcItemDrop::item);
        QCOMPARE(feathers, 2);
        QVERIFY(std::ranges::find(chicken().drops, u"qtcraft:raw_chicken"_s, &QcItemDrop::item) != chicken().drops.end());

        // The egg is thrown and breaks; raw chicken cooks in the furnace.
        const QcItemDefinition &egg = m_content->item(*m_content->itemId(u"qtcraft:egg"_s));
        QVERIFY(egg.throwable && egg.projectile == u"qtcraft:egg"_s);
        QVERIFY(m_content->projectile(u"qtcraft:egg"_s)->breaks());
        QVERIFY(std::ranges::any_of(m_content->smeltingRecipes(),
                                    [](const QcSmeltingRecipe &recipe) { return recipe.inputItem == u"qtcraft:raw_chicken"_s; }));
    }

    void zombiesDropNoFeathers()
    {
        const QList<QcItemDrop> &zombie = m_content->mobs()[*m_content->mobIndex(u"qtcraft:zombie"_s)].drops;
        QVERIFY(std::ranges::find(zombie, u"qtcraft:feather"_s, &QcItemDrop::item) == zombie.end());
    }

    void chickensFlutterDownSlowly()
    {
        join();
        for (int dy = 0; dy <= 14; ++dy) {
            m_server->world().setBlock(m_feet.offset(3, dy, 3), qcAirBlockId);
        }
        const quint32 bird = spawn(3, 12, 3);
        double slowest = 0.0;
        int ticksInAir = 0;
        for (int i = 0; i < 200 && !mob(bird)->motion.onGround; ++i) {
            ticks(1);
            slowest = std::min(slowest, mob(bird)->motion.velocity.y);
            ++ticksInAir;
        }
        QVERIFY(mob(bird)->motion.onGround);
        QVERIFY2(slowest >= -chicken().maxFallSpeed - 1e-9, qPrintable(QString::number(slowest)));
        // Twelve blocks at 2.5 m/s take about 4.8 s; a free fall would take 1.6 s.
        QVERIFY2(ticksInAir >= 90, qPrintable(QString::number(ticksInAir)));
    }

    void fallLimitHoldsOnEveryPath()
    {
        // Lying (a pose whose brain may let it rest) and pushed down hard: still no faster.
        join();
        for (int dy = 0; dy <= 14; ++dy) {
            m_server->world().setBlock(m_feet.offset(3, dy, 3), qcAirBlockId);
        }
        const quint32 bird = spawn(3, 12, 3);
        mob(bird)->pose = QcPose::Lying;
        mob(bird)->motion.velocity.y = -20.0;
        ticks(1);
        QVERIFY2(mob(bird)->motion.velocity.y >= -chicken().maxFallSpeed - 1e-9, qPrintable(QString::number(mob(bird)->motion.velocity.y)));
    }

    void layingTimerIsSaved()
    {
        join();
        QcCreature hen;
        hen.type = chickenType();
        hen.uid = 9;
        hen.health = 4.0;
        hen.motion.position = {3.5, 70.0, 4.5};
        hen.layTimer = 123.25;
        const QcMobManager::Stored stored = QcMobManager::storedOf(hen);
        QCOMPARE(stored.layTimer, 123.25);

        // Through the chunk's entity record, and a timer not chosen yet as not chosen.
        QcEntityRecord record;
        record.chunk = {0, 0};
        record.creatures.append({stored, 1});
        QcMobManager::Stored unchosen = stored;
        unchosen.uid = 10;
        unchosen.layTimer = -1.0;
        record.creatures.append({unchosen, 2});
        QcLoadWarnings warnings;
        const QcResult<QcEntityRecord> read =
            QcEntityCodec::decode(QcEntityCodec::encode(record, *m_content), {0, 0}, *m_content, warnings);
        QVERIFY(read);
        QCOMPARE(read->creatures.size(), 2);
        QCOMPARE(read->creatures.at(0).entry.layTimer, 123.25);
        QCOMPARE(read->creatures.at(1).entry.layTimer, -1.0);

        // Back in the world it lays on the saved time.
        QcMobManager &creatures = m_server->entities().creatures();
        creatures.restore({read->creatures.at(0).entry}, {}, [this] { return m_server->entities().newId(); });
        const auto restored = std::ranges::find(creatures.mobs(), quint64(9), &QcCreature::uid);
        QVERIFY(restored != creatures.mobs().end());
        QCOMPARE(restored->layTimer, 123.25);
    }

    void adultsLayEggsOnTheirTimer()
    {
        join();
        const QcItemId egg = item("qtcraft:egg");
        const quint32 hen = spawn(3, 0, 3);
        const quint32 chick = spawn(-3, 0, -3);
        mob(chick)->growUp = 1000.0;
        ticks(1);
        // The first tick chooses the time between five and ten minutes.
        QVERIFY(mob(hen)->layTimer >= 300.0 && mob(hen)->layTimer <= 600.0);
        QCOMPARE(mob(chick)->layTimer, -1.0);

        mob(hen)->layTimer = 0.12;
        ticks(2);
        QCOMPARE(droppedCount(egg), 0);
        ticks(1);
        QCOMPARE(droppedCount(egg), 1);
        QVERIFY(mob(hen)->layTimer >= 299.9 && mob(hen)->layTimer <= 600.0);

        // Over a long time the eggs come every 7.5 minutes on average.
        QcContentRegistry content = *m_content;
        QList<QcCreature> hens(20);
        for (QcCreature &one : hens) {
            one.type = chickenType();
        }
        QRandomGenerator random(52);
        int laid = 0;
        const int hours = 10;
        for (int i = 0; i < hours * 3600; ++i) {
            laid += int(QcMobLaying::tick(content, hens, random, 1.0).size());
        }
        // 20 hens for ten hours: 1600 eggs expected.
        QVERIFY2(laid > 1500 && laid < 1700, qPrintable(QString::number(laid)));
    }

    void eggsHatchAtTheSeededRate()
    {
        const QcProjectileDefinition *egg = m_content->projectile(u"qtcraft:egg"_s);
        QVERIFY(egg);
        const auto spawn = std::ranges::find_if(egg->onHit, [](const QcHitEffect &effect) { return effect.is(QcHitEffect::spawn); });
        QVERIFY(spawn != egg->onHit.end());
        QCOMPARE(spawn->subject, u"qtcraft:chicken"_s);

        const auto throwMany = [&](quint32 seed) {
            QRandomGenerator random(seed);
            QList<int> outcomes(5, 0);
            for (int i = 0; i < 64000; ++i) {
                ++outcomes[QcHitEffects::spawnCount(*spawn, random)];
            }
            return outcomes;
        };
        const QList<int> outcomes = throwMany(450);
        // One throw in eight hatches, a 32nd of those four chicks: 7750 single chicks, 250 groups.
        QCOMPARE(outcomes[2] + outcomes[3], 0);
        QVERIFY2(outcomes[1] > 7450 && outcomes[1] < 8050, qPrintable(QString::number(outcomes[1])));
        QVERIFY2(outcomes[4] > 190 && outcomes[4] < 310, qPrintable(QString::number(outcomes[4])));
        // The same seed gives the same chicks.
        QCOMPARE(throwMany(450), outcomes);
    }

    void chickensBreedWithSeeds()
    {
        join();
        const quint32 first = spawn(2, 0, 0);
        const quint32 second = spawn(-2, 0, 1);
        chat(u"/give wheat_seeds 2"_s);
        hold("qtcraft:wheat_seeds");
        send(QcMsg::UseEntity{first});
        send(QcMsg::UseEntity{second});
        QVERIFY(mob(first)->love > 0.0 && mob(second)->love > 0.0);
        QCOMPARE(countOf(item("qtcraft:wheat_seeds")), 0);
        for (int i = 0; i < 100 && chickens() < 3; ++i) {
            ticks(1);
        }
        QCOMPARE(chickens(), 3);
        const auto young = std::ranges::find_if(m_server->entities().creatures().mobs(), &QcCreature::isYoung);
        QVERIFY(young != m_server->entities().creatures().mobs().end() && young->type == chickenType());

        // The chick grows up when its time is over, and then lays too.
        const quint32 chick = young->id;
        mob(chick)->growUp = 0.1;
        ticks(3);
        QVERIFY(!mob(chick)->isYoung());
        QVERIFY(mob(chick)->layTimer >= 300.0);
    }
};

QTEST_MAIN(TestChickens)
#include "test_chickens.moc"
