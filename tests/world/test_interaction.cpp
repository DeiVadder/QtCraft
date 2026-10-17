#include "common/gameplay/QcInventory.h"
#include "common/gameplay/QcMining.h"
#include "common/physics/QcRaycast.h"
#include "QcTestWorld.h"

#include <QTest>

using namespace Qt::StringLiterals;

namespace {

QcContentRegistry contentWithItems()
{
    QcContentRegistry content = qcMakeTestContent();
    content.createBlockItems();

    QcItemDefinition pickaxe;
    pickaxe.name = u"test:pickaxe"_s;
    pickaxe.maxStack = 1;
    pickaxe.tool = u"pickaxe"_s;
    pickaxe.toolLevel = 1;
    pickaxe.toolSpeed = 4.0;
    pickaxe.durability = 10;
    if (!content.registerItem(pickaxe)) {
        qFatal("cannot register pickaxe");
    }
    return content;
}

} // namespace

class TestInteraction : public QObject
{
    Q_OBJECT

private:
    QcContentRegistry m_content = contentWithItems();
    QcItemId stoneItem() const { return *m_content.itemId(u"test:stone"_s); }

private slots:
    void blockItemsExistOnlyForVisibleSolidBlocks()
    {
        QVERIFY(m_content.itemId(u"test:stone"_s).has_value());
        QCOMPARE(m_content.item(stoneItem()).placesBlock, u"test:stone"_s);
        QVERIFY(!m_content.itemId(u"test:water"_s).has_value());
        QVERIFY(!m_content.itemId(u"qtcraft:air"_s).has_value());
    }

    void addFillsStacksThenEmptySlotsHotbarFirst()
    {
        QcInventory inventory;
        QCOMPARE(inventory.add(stoneItem(), 100, m_content), 0);
        QCOMPARE(inventory.slot(0).count, 64);
        QCOMPARE(inventory.slot(1).count, 36);
        QCOMPARE(inventory.add(stoneItem(), 30, m_content), 0);
        QCOMPARE(inventory.slot(1).count, 64);
        QCOMPARE(inventory.slot(2).count, 2);
        QCOMPARE(inventory.countOf(stoneItem()), 130);
    }

    void addReportsWhatDoesNotFit()
    {
        QcInventory inventory;
        QCOMPARE(inventory.add(stoneItem(), 64 * QcInventory::slotCount + 5, m_content), 5);
        QCOMPARE(inventory.add(qcNoItem, 3, m_content), 3);
    }

    void moveMergesUpToTheLimitThenSwaps()
    {
        QcInventory inventory;
        inventory.setSlot(0, {stoneItem(), 50, 0});
        inventory.setSlot(5, {stoneItem(), 30, 0});
        inventory.move(5, 0, m_content);
        QCOMPARE(inventory.slot(0).count, 64);
        QCOMPARE(inventory.slot(5).count, 16);

        const QcItemId pickaxe = *m_content.itemId(u"test:pickaxe"_s);
        inventory.setSlot(7, {pickaxe, 1, 3});
        inventory.move(7, 5, m_content);
        QCOMPARE(inventory.slot(5).item, pickaxe);
        QCOMPARE(inventory.slot(5).damage, 3);
        QCOMPARE(inventory.slot(7).count, 16);
    }

    void removeItemsIsAllOrNothing()
    {
        QcInventory inventory;
        inventory.setSlot(3, {stoneItem(), 4, 0});
        inventory.setSlot(9, {stoneItem(), 4, 0});
        QVERIFY(!inventory.removeItems(stoneItem(), 9));
        QCOMPARE(inventory.countOf(stoneItem()), 8);
        QVERIFY(inventory.removeItems(stoneItem(), 6));
        QCOMPARE(inventory.countOf(stoneItem()), 2);
    }

    void raycastHitsFirstSolidBlockAndReportsTheFace()
    {
        QcTestWorld world(m_content);
        world.set({5, 64, 0}, 1);
        world.set({8, 64, 0}, 1);

        const auto hit = QcRaycast::cast(world, {0.5, 64.5, 0.5}, {1.0, 0.0, 0.0}, 10.0);
        QVERIFY(hit.has_value());
        QCOMPARE(hit->block, (QcBlockPos{5, 64, 0}));
        QCOMPARE(hit->face, QcBlockFace::West);
        QVERIFY(std::abs(hit->distance - 4.5) < 1e-9);

        const auto fromAbove = QcRaycast::cast(world, {5.5, 70.0, 0.5}, {0.0, -1.0, 0.0}, 10.0);
        QVERIFY(fromAbove.has_value());
        QCOMPARE(fromAbove->face, QcBlockFace::Up);
    }

    void raycastIgnoresLiquidsAndRespectsReach()
    {
        QcTestWorld world(m_content);
        world.set({2, 64, 0}, 2);
        world.set({4, 64, 0}, 1);
        QCOMPARE(QcRaycast::cast(world, {0.5, 64.5, 0.5}, {1.0, 0.0, 0.0}, 10.0)->block, (QcBlockPos{4, 64, 0}));
        QVERIFY(!QcRaycast::cast(world, {0.5, 64.5, 0.5}, {1.0, 0.0, 0.0}, 3.0).has_value());
        QVERIFY(!QcRaycast::cast(world, {0.5, 64.5, 0.5}, {0.0, 0.0, 0.0}, 3.0).has_value());
    }

    void miningTimeDependsOnHardnessAndTool()
    {
        QcBlockDefinition stone;
        stone.hardness = 1.5;
        stone.tool = u"pickaxe"_s;
        stone.toolLevel = 1;
        const QcItemDefinition &pickaxe = m_content.item(*m_content.itemId(u"test:pickaxe"_s));
        // Needs a pickaxe: by hand 1.5 * 5 s and no drops; with a tier-1 pickaxe at speed 4: 1.5 * 1.5 / 4 s.
        QCOMPARE(QcMining::breakSeconds(stone, nullptr), 7.5);
        QVERIFY(!QcMining::canHarvest(stone, nullptr));
        QCOMPARE(QcMining::breakSeconds(stone, &pickaxe), 0.5625);
        QVERIFY(QcMining::canHarvest(stone, &pickaxe));

        QcBlockDefinition dirt;
        dirt.hardness = 0.5;
        dirt.tool = u"shovel"_s;
        QCOMPARE(QcMining::breakSeconds(dirt, nullptr), 0.75);
        QCOMPARE(QcMining::breakSeconds(dirt, &pickaxe), 0.75);
        QVERIFY(QcMining::canHarvest(dirt, nullptr));

        QcBlockDefinition bedrock;
        bedrock.hardness = -1.0;
        QVERIFY(QcMining::breakSeconds(bedrock, nullptr) < 0.0);

        QcBlockDefinition flower;
        flower.hardness = 0.0;
        QCOMPARE(QcMining::breakSeconds(flower, nullptr), QcMining::minimumSeconds);
    }
};

QTEST_GUILESS_MAIN(TestInteraction)
#include "test_interaction.moc"
