#include "QcTestUtil.h"
#include "server/entities/QcEntities.h"
#include "server/storage/QcEntityCodec.h"
#include "server/storage/QcEntityRecords.h"

#include <QTest>

using namespace Qt::StringLiterals;

namespace {

// What a newer version would have put into an entry of `kind` (decision E-420): more fields of its
// own component (a tail), a component of an unknown id and the header fields this version leaves
// unused (`withType`: the type name as well as the persistent id).
QcEntityKept newerData(QcEntityCodec::Component kind, quint16 unknownId, bool withType)
{
    QcEntityKept kept;
    kept.components = {{quint16(kind), 3, true, QByteArray("tail of a newer version")}, {unknownId, 2, false, QByteArray("component of a mod")}};
    kept.uid = 4000 + unknownId;
    kept.type = withType ? u"newer:kind"_s : QString();
    return kept;
}

QcEntityRecord decoded(const QByteArray &blob, const QcContentRegistry &content)
{
    QcLoadWarnings warnings;
    QcResult<QcEntityRecord> record = QcEntityCodec::decode(blob, {0, 0}, content, warnings);
    if (!record) {
        qWarning() << qcErrorText(record);
        return {};
    }
    return *record;
}

} // namespace

// What a saved entry holds that this version does not use (decision E-420) survives the runtime
// objects: decoded, put into the world, waiting in the item queue, spawned, collected and encoded
// again, for every kind. Also: a tail stays as it was when the fields before it change, more unknown
// components than an entry keeps are dropped with a warning, and copied creatures get unique ids.
class TestEntityComponents : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    QcEntities::Clients m_clients;

    // A chunk with one entity of every kind, each with the data of a newer version.
    QcEntityRecord newerRecord() const
    {
        using C = QcEntityCodec::Component;
        QcEntityRecord record;
        record.chunk = {0, 0};

        QcMobManager::Stored pig{.type = *m_content->mobIndex(u"qtcraft:pig"_s), .position = {8.5, 65.0, 8.5}, .yaw = 45.0f, .health = 7.5, .villager = std::nullopt, .uid = 77};
        pig.kept = newerData(C::Creature, 900, false);
        pig.kept.uid = 0;
        record.creatures = {{pig, 0}};

        QcItemEntityManager::Drop drop{{*m_content->itemId(u"qtcraft:stick"_s), 3, 0}, {1.5, 64.0, 2.5}, {}, 0.0, 12.5};
        drop.kept = newerData(C::Item, 901, false);
        record.items = {{drop, 0}};

        QcFallingBlock falling;
        falling.block = *m_content->blockId(u"qtcraft:sand"_s);
        falling.motion.position = {3.5, 71.25, 9.5};
        falling.kept = newerData(C::FallingBlock, 902, false);
        record.fallingBlocks = {{falling, 0}};

        QcArmourStand stand;
        stand.motion.position = {4.5, 63.0, 4.5};
        stand.item = u"qtcraft:armor_stand"_s;
        stand.kept = newerData(C::ArmourStand, 903, true);
        record.stands = {{stand, 0}};

        QcMinecart cart;
        cart.motion.position = {6.5, 64.0, 6.5};
        cart.kept = newerData(C::Minecart, 904, true);
        record.minecarts = {{cart, 0}};

        QcExperienceOrb orb;
        orb.motion.position = {10.5, 64.0, 10.5};
        orb.value = 5;
        orb.kept = newerData(C::ExperienceOrb, 905, true);
        record.orbs = {{orb, 0}};
        return record;
    }

    // `record` encoded, decoded, put into a fresh world, collected and encoded again; the record that
    // comes back of that.
    QcEntityRecord throughTheWorld(const QcEntityRecord &record)
    {
        QcEntities entities(m_content, 1, m_clients);
        const QStringList warnings = QcEntityRecords::apply(decoded(QcEntityCodec::encode(record, *m_content), *m_content), entities);
        if (!warnings.isEmpty()) {
            qWarning() << warnings;
        }
        return decoded(QcEntityCodec::encode(QcEntityRecords::collect(entities).value({0, 0}), *m_content), *m_content);
    }

private slots:
    void newerDataSurvivesTheRuntimeObjectsOfEveryKind()
    {
        const QcEntityRecord record = newerRecord();
        const QcEntityRecord back = throughTheWorld(record);

        QCOMPARE(back.creatures.size(), 1);
        QVERIFY(back.creatures.first().entry.kept == record.creatures.first().entry.kept);
        QCOMPARE(back.creatures.first().entry.uid, 77u);
        QCOMPARE(back.items.size(), 1);
        QVERIFY(back.items.first().entry.kept == record.items.first().entry.kept);
        QCOMPARE(back.fallingBlocks.size(), 1);
        QVERIFY(back.fallingBlocks.first().entry.kept == record.fallingBlocks.first().entry.kept);
        QCOMPARE(back.stands.size(), 1);
        QVERIFY(back.stands.first().entry.kept == record.stands.first().entry.kept);
        QCOMPARE(back.minecarts.size(), 1);
        QVERIFY(back.minecarts.first().entry.kept == record.minecarts.first().entry.kept);
        QCOMPARE(back.orbs.size(), 1);
        QVERIFY(back.orbs.first().entry.kept == record.orbs.first().entry.kept);
        QCOMPARE(back.stands.first().entry.kept.type, u"newer:kind"_s);
        QCOMPARE(back.orbs.first().entry.kept.uid, 4905u);
    }

    // A drop that waits in the queue (beyond the total limit) keeps its data when it is spawned.
    void aQueuedDropKeepsNewerData()
    {
        const QcEntityRecord record = newerRecord();
        QcEntities entities(m_content, 1, m_clients);
        entities.items().setLimits(10, 10, 0);
        QcEntityRecords::apply(decoded(QcEntityCodec::encode(record, *m_content), *m_content), entities);
        QCOMPARE(entities.items().items().size(), 0);
        QCOMPARE(entities.items().queued().size(), 1);

        entities.items().setLimits(10, 10, 10);
        const QList<QcItemEntityManager::Drop> waiting = entities.items().takeQueued([](const QcItemEntityManager::Drop &) { return true; });
        QVERIFY(entities.items().spawn(waiting.first(), entities.newId()) != 0);
        QCOMPARE(entities.items().items().size(), 1);

        const QcEntityRecord back = decoded(QcEntityCodec::encode(QcEntityRecords::collect(entities).value({0, 0}), *m_content), *m_content);
        QCOMPARE(back.items.size(), 1);
        QVERIFY(back.items.first().entry.kept == record.items.first().entry.kept);
    }

    // A drop larger than a stack is split; only the first part keeps the data, a copy for every part
    // would multiply it.
    void onlyTheFirstPartOfASplitDropKeepsNewerData()
    {
        QcEntities entities(m_content, 1, m_clients);
        QcItemEntityManager::Drop drop{{*m_content->itemId(u"qtcraft:stick"_s), 150, 0}, {1.5, 64.0, 2.5}, {}, 0.0, 0.0};
        drop.kept = newerData(QcEntityCodec::Component::Item, 901, false);
        QVERIFY(entities.items().spawn(drop, entities.newId()) != 0);

        QCOMPARE(entities.items().items().size(), 1);
        QVERIFY(entities.items().items().first().kept == drop.kept);
        QCOMPARE(entities.items().queued().size(), 2);
        for (const QcItemEntityManager::Drop &part : entities.items().queued()) {
            QVERIFY(part.kept.isEmpty());
        }
    }

    // The tail of a component goes back unchanged while the fields before it changed at runtime.
    void aTailStaysWhenTheFieldsBeforeItChange()
    {
        QcEntityRecord record = newerRecord();
        QcEntities entities(m_content, 1, m_clients);
        QcEntityRecords::apply(decoded(QcEntityCodec::encode(record, *m_content), *m_content), entities);
        entities.creatures().editableMobs().first().health = 2.5;

        const QcEntityRecord back = decoded(QcEntityCodec::encode(QcEntityRecords::collect(entities).value({0, 0}), *m_content), *m_content);
        QCOMPARE(back.creatures.first().entry.health, 2.5);
        QVERIFY(back.creatures.first().entry.kept == record.creatures.first().entry.kept);
        QCOMPARE(back.creatures.first().entry.kept.components.first().version, quint16(3));
        QCOMPARE(back.creatures.first().entry.kept.components.first().payload, QByteArray("tail of a newer version"));
    }

    // A chunk copied next to its original brings creatures with ids already in the world: the copies
    // get new ones beyond every given id.
    void copiedCreaturesGetUniqueIds()
    {
        QcEntityRecord record = newerRecord();
        QcEntities entities(m_content, 1, m_clients);
        QcEntityRecords::apply(record, entities);
        QTest::ignoreMessage(QtWarningMsg, "A loaded creature has the persistent id 77 of another one; it gets 78");
        QcEntityRecords::apply(record, entities);

        const QList<QcCreature> &mobs = entities.creatures().mobs();
        QCOMPARE(mobs.size(), 2);
        QCOMPARE(mobs[0].uid, 77u);
        QCOMPARE(mobs[1].uid, 78u);
        QCOMPARE(entities.creatures().nextUid(), 79u);
    }

    // Setting the counter (from world.json) never lowers it below an id already given.
    void theIdCounterNeverGoesBack()
    {
        QcEntities entities(m_content, 1, m_clients);
        QcEntityRecords::apply(newerRecord(), entities);
        QCOMPARE(entities.creatures().nextUid(), 78u);
        entities.creatures().setNextUid(5);
        QCOMPARE(entities.creatures().nextUid(), 78u);
        entities.creatures().setNextUid(500);
        QCOMPARE(entities.creatures().nextUid(), 500u);
    }
};

QTEST_GUILESS_MAIN(TestEntityComponents)
#include "test_entity_components.moc"
