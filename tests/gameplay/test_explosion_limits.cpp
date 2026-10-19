#include "QcMachineFixture.h"
#include "server/explosions/QcBlastBlocks.h"
#include "server/explosions/QcServerExplosions.h"
#include "server/storage/QcEntityRecords.h"

using namespace Qt::StringLiterals;

// Explosions at their limits (stage 51.2, decisions E-601, E-602): what must not get lost when a
// limit holds back something else. A chest in a chain keeps its contents while rubble fills the
// queue of waiting drops, the clients never get a batch above their limit, lit explosives beyond
// the limit of a loading chunk come back as explosive blocks, and fire beside an explosive that
// cannot be lit leaves it standing. Ticks are stepped by hand.
class TestExplosionLimits : public QcMachineFixture
{
    Q_OBJECT

private:
    QcServerExplosions &explosions() { return m_server->systems().explosions; }
    QcBlockId block(QcBlockPos pos) const { return m_server->world().blockAt(pos).value_or(qcAirBlockId); }
    qsizetype primed() const { return m_server->entities().explosives().all().size(); }

    // Items of `name` lying in the world or waiting to appear.
    int itemsOf(const char *name) const
    {
        const QcItemEntityManager &items = m_server->entities().items();
        int count = 0;
        for (const QcItemEntity &entity : items.items()) {
            count += entity.stack.item == item(name) ? entity.stack.count : 0;
        }
        for (const QcItemEntityManager::Drop &drop : items.queued()) {
            count += drop.stack.item == item(name) ? drop.stack.count : 0;
        }
        return count;
    }

    // A lit explosive of the base content at the middle of local cell (x, y, z) with `fuse` ticks.
    QcEntityRecord::Ordered<QcPrimedExplosive> litAt(int x, int y, int z, int fuse, quint64 order) const
    {
        QcPrimedExplosive primed;
        primed.block = id("qtcraft:explosive");
        primed.fuse = fuse;
        const QcBlockPos pos = at(x, y, z);
        primed.motion.position = {pos.x + 0.5, double(pos.y), pos.z + 0.5};
        return {primed, order};
    }

private slots:
    void chestInAChainKeepsItsContents()
    {
        QcServerConfig config;
        config.limits.itemsAwake = 4;
        config.limits.itemsQueued = 8;
        buildArea(config);
        // A field of 12 × 12 × 2 explosives with a chest in its upper layer.
        for (int x = 1; x < 13; ++x) {
            for (int z = 1; z < 13; ++z) {
                put(x, 0, z, u"explosive"_s);
                put(x, 1, z, u"explosive"_s);
            }
        }
        put(6, 1, 6, u"chest"_s);
        fill(6, 1, 6, {stack("qtcraft:diamond", 5), stack("qtcraft:emerald", 7), stack("qtcraft:gold_ingot", 3)});
        QVERIFY(explosions().light(at(1, 0, 1), 1));

        int tick = 0;
        for (; tick < 3000 && (primed() > 0 || explosions().waitingCells() > 0); ++tick) {
            ticks(1);
        }
        QVERIFY2(tick < 3000, "the chain did not end");
        QVERIFY(block(at(6, 1, 6)) != id("qtcraft:chest"));
        // The rubble filled the queue (some of it was held back), the chest's contents all came out.
        QVERIFY(m_server->entities().items().queued().size() >= 4);
        QCOMPARE(itemsOf("qtcraft:diamond"), 5);
        QCOMPARE(itemsOf("qtcraft:emerald"), 7);
        QCOMPARE(itemsOf("qtcraft:gold_ingot"), 3);
    }

    void batchesStayWithinTheClientsLimit()
    {
        // More blocks in a tick than a client takes in one batch (explosionBlocksPerTick raised far
        // above QcMsg::BlockBatch::maxUpdates) go out in several batches, in order.
        constexpr qsizetype most = qsizetype(QcMsg::BlockBatch::maxUpdates);
        QList<QcMsg::BlockUpdate> updates;
        for (qsizetype i = 0; i < 2 * most + 1; ++i) {
            updates.append({QcBlockPos{int(i % 4096), int(i / 4096), 0}, qcAirBlockId});
        }
        const QList<QcMsg::BlockBatch> batches = QcBlastBlocks::pieces(updates, most);
        QCOMPARE(batches.size(), qsizetype(3));
        QList<QcMsg::BlockUpdate> joined;
        for (const QcMsg::BlockBatch &batch : batches) {
            QVERIFY(batch.updates.size() <= most);
            joined.append(batch.updates);
        }
        QCOMPARE(joined.size(), updates.size());
        for (qsizetype i = 0; i < joined.size(); ++i) {
            QVERIFY(joined[i].pos == updates[i].pos);
        }
        QVERIFY(QcBlastBlocks::pieces({}, most).isEmpty());
    }

    void litExplosivesBeyondTheLimitLandAsBlocks()
    {
        QcServerConfig config;
        config.limits.primedExplosives = 1;
        buildArea(config);
        // A chunk comes back with three lit explosives: one fits, two fall on as explosive blocks.
        QcEntityRecord record;
        record.chunk = at(2, 0, 7).chunk();
        record.primedExplosives = {litAt(2, 3, 7, 600, 1), litAt(7, 3, 7, 600, 2), litAt(12, 3, 7, 600, 3)};
        const QStringList warnings = QcEntityRecords::apply(record, m_server->entities());
        QCOMPARE(warnings.size(), qsizetype(1));
        QCOMPARE(primed(), qsizetype(1));
        QCOMPARE(m_server->entities().fallingBlocks().all().size(), qsizetype(2));

        ticks(60);
        QCOMPARE(primed(), qsizetype(1));
        QCOMPARE(block(at(7, 0, 7)), id("qtcraft:explosive"));
        QCOMPARE(block(at(12, 0, 7)), id("qtcraft:explosive"));
        QCOMPARE(block(at(2, 0, 7)), qcAirBlockId);
    }

    void oresBrokenByABlastGiveNoExperience()
    {
        buildArea();
        // An explosive in a block of coal ore: some ore drops its coal, none gives experience.
        for (int x = 4; x < 9; ++x) {
            for (int y = 0; y < 3; ++y) {
                for (int z = 4; z < 9; ++z) {
                    put(x, y, z, u"coal_ore"_s);
                }
            }
        }
        put(6, 1, 6, u"explosive"_s);
        QVERIFY(explosions().light(at(6, 1, 6), 1));
        for (int tick = 0; tick < 100 && (primed() > 0 || explosions().waitingCells() > 0); ++tick) {
            ticks(1);
        }
        QVERIFY(explosions().blocksBroken() > 20);
        QVERIFY(itemsOf("qtcraft:coal") > 0);
        QVERIFY(m_server->entities().orbs().orbs().isEmpty());
    }

    void fireAtTheLimitLeavesTheExplosive()
    {
        QcServerConfig config;
        config.limits.primedExplosives = 0;
        buildArea(config);
        // Fire that would light the explosive but may not (no lit explosive fits) keeps burning
        // beside it; the explosive never burns away.
        put(5, 0, 5, u"explosive"_s);
        QVERIFY(m_server->systems().fire.ignite(at(5, 0, 6)));
        for (int tick = 0; tick < 1500; ++tick) {
            ticks(1);
            if (block(at(5, 0, 6)) == qcAirBlockId) {
                m_server->systems().fire.ignite(at(5, 0, 6));
            }
        }
        QVERIFY(explosions().numbers().refused > 0);
        QCOMPARE(explosions().numbers().lit, quint64(0));
        QCOMPARE(block(at(5, 0, 5)), id("qtcraft:explosive"));
    }
};

QTEST_MAIN(TestExplosionLimits)
#include "test_explosion_limits.moc"
