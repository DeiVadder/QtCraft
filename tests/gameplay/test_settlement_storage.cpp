#include "QcServerTestFixture.h"
#include "server/villages/QcVillagePopulation.h"
#include "server/villages/QcVillagerHomes.h"
#include "server/villages/QcVillages.h"

#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

using namespace Qt::StringLiterals;

// Settlements (decision E-490) across saving and loading, on a server: a settlement grown by a
// second bed keeps its id, style, area and claims, and its claims are reconciled after loading.
class TestSettlementStorage : public QcServerTestFixture
{
    Q_OBJECT

private:
    int villagerType() const { return *m_content->mobIndex(u"qtcraft:villager"_s); }

    QcBlockId state(const QString &name) const { return *m_content->stateId(name); }

    // A bed lying north (head at `foot` - z); returns the head.
    QcBlockPos bed(QcBlockPos foot)
    {
        m_server->world().setBlock(foot, state(u"qtcraft:bed[facing=north,part=foot]"_s));
        m_server->world().setBlock(foot.offset(0, 0, -1), state(u"qtcraft:bed[facing=north,part=head]"_s));
        return foot.offset(0, 0, -1);
    }

    static QcVec3 centreOf(QcBlockPos cell) { return {cell.x + 0.5, static_cast<double>(cell.y), cell.z + 0.5}; }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    // A settlement grown by a second bed (decision E-490) is saved with its id, style, area and
    // claims, and after loading its claims are reconciled with its villager in the world.
    void aGrownSettlementIsSavedAndLoaded()
    {
        QTemporaryDir directory;
        QcServerConfig config;
        config.worldDirectory = directory.path();
        join(config);
        chat(u"/gamerule mobSpawning false"_s);
        const QcBlockPos ground = m_feet.offset(120, 0, 0);
        loadAround(ground, 3);
        const int east = QcVillage::reach + 8;
        for (int dx = -2; dx <= east + 2; ++dx) {
            for (int dz = -3; dz <= 3; ++dz) {
                m_server->world().setBlock(ground.offset(dx, -1, dz), id("qtcraft:stone"));
                for (int dy = 0; dy <= 3; ++dy) {
                    m_server->world().setBlock(ground.offset(dx, dy, dz), qcAirBlockId);
                }
            }
        }
        const QcBlockPos first = bed(ground);
        const QcBlockPos second = bed(ground.offset(east, 0, 0));
        QcVillages &villages = m_server->world().villages();
        QCOMPARE(villages.count(), 1);
        QcVillage *settlement = villages.find(QcVillage::settlementId(first));
        QVERIFY(settlement);
        QCOMPARE(settlement->maxX, second.x);
        QCOMPARE(settlement->beds.size(), 2);

        const quint32 entity = m_server->entities().newId();
        m_server->mobs().spawn(villagerType(), centreOf(ground.offset(2, 0, 1)), entity);
        QcCreature *settler = m_server->mobs().find(entity);
        settler->villager->style = u"qtcraft:plains"_s;
        QcVillagerHomes::moveIn(*settlement, *settler->villager, settler->uid, settler->motion.position);
        const quint64 uid = settler->uid;
        QCOMPARE(settlement->style, u"qtcraft:plains"_s);
        QCOMPARE(settler->villager->home, std::optional(first));
        const QList<QcVillage> saved = villages.villages();
        QVERIFY(m_server->save().has_value());
        cleanup();

        join(config);
        loadAround(m_feet, 2);
        // Every chunk its villager may stand in (QcVillagerHomes::strayReach).
        loadAround(ground, 5);
        QCOMPARE(m_server->world().villages().villages(), saved);
        ticks(QcVillagePopulation::villageLifeTicks + 2);
        const QcVillage *loaded = m_server->world().villages().find(QcVillage::settlementId(first));
        QVERIFY(loaded);
        QVERIFY(m_server->mobs().population().claimsReconciled(loaded->id));
        QCOMPARE(loaded->style, u"qtcraft:plains"_s);
        QCOMPARE(loaded->population, 1);
        const auto bedOf = std::ranges::find(loaded->beds, first, &QcVillage::Spot::at);
        QVERIFY(bedOf != loaded->beds.end());
        QCOMPARE(bedOf->claimedBy, uid);
        const auto found = std::ranges::find(m_server->mobs().mobs(), uid, &QcCreature::uid);
        QVERIFY(found != m_server->mobs().mobs().end());
        QCOMPARE(found->villager->village, loaded->id);
    }

};

QTEST_GUILESS_MAIN(TestSettlementStorage)
#include "test_settlement_storage.moc"
