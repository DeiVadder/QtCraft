#include "QcCircuitFixture.h"
#include "QcTestUtil.h"

#include <QTemporaryDir>

using namespace Qt::StringLiterals;

// Item stacks keep their data (decision E-823) on every way through a running server: chests,
// items on the ground, the player's file and the wire, hoppers, death drops; creative requests keep
// only a filled map's id.
class TestItemDataPaths : public QcCircuitFixture
{
    Q_OBJECT

    QcServerClient &player() { return *m_server->entities().clients().front(); }

    QcItemStack named(const char *name, int count, const QString &text) const { return QcItemStack{item(name), count, 0}.withName(text); }

    QcItemStack holding(QList<QcItemStack> contents) const { return QcItemStack{item("qtcraft:stick"), 1, 0}.withContents(std::move(contents)); }

    QcItemStack map(quint32 id) const { return QcItemStack{item("qtcraft:filled_map"), 1, 0}.withMapId(id); }

    // How many items on the ground are `wanted` (count included).
    int onTheGround(const QcItemStack &wanted) const
    {
        int found = 0;
        for (const QcItemEntity &dropped : m_server->items().items()) {
            found += dropped.stack == wanted ? 1 : 0;
        }
        return found;
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void dataSurvivesAWorldRestart()
    {
        QTemporaryDir directory;
        QcServerConfig config;
        config.worldDirectory = directory.path();
        config.autosaveSeconds = 0;
        buildArea(config);
        const QcItemStack full = holding({named("qtcraft:dirt", 3, u"Soil"_s), {}, map(4)});
        const QcItemStack soil = named("qtcraft:dirt", 7, u"Soil"_s);
        put(2, 0, 2, u"chest[facing=south]"_s);
        m_server->blockEntities().container(at(2, 0, 2))->stacks[0] = full;
        m_server->blockEntities().container(at(2, 0, 2))->stacks[1] = map(5);
        const QcBlockPos ground = at(6, 0, 6);
        m_server->items().spawn({soil, {ground.x + 0.5, double(ground.y), ground.z + 0.5}, {}}, m_server->entities().newId());
        player().inventory.setSlot(4, full);
        const QcItemStack helmet = named("qtcraft:iron_helmet", 1, u"Lid"_s);
        const QcItemDefinition &helmetItem = m_content->item(helmet.item);
        QVERIFY(helmetItem.armour);
        player().equipment.stacks[std::size_t(helmetItem.armour->slot)] = helmet;
        QVERIFY(m_server->save().has_value());
        const QcBlockPos origin = m_origin;
        cleanup();

        join(config);
        m_origin = origin;
        loadAround(at(2, 0, 2));
        loadAround(ground);
        QCOMPARE(m_server->blockEntities().container(at(2, 0, 2))->stacks.at(0), full);
        QCOMPARE(m_server->blockEntities().container(at(2, 0, 2))->stacks.at(1), map(5));
        QCOMPARE(onTheGround(soil), 1);
        // The player's file, then the wire.
        QCOMPARE(player().inventory.slot(4), full);
        QCOMPARE(m_client->stacks.value(4), full);
        QCOMPARE(m_client->equipment.stacks[std::size_t(helmetItem.armour->slot)], helmet);
    }

    void hoppersMoveStacksWithTheirData()
    {
        buildArea();
        put(2, 1, 2, u"chest[facing=south]"_s);
        const QcItemStack soil = named("qtcraft:dirt", 2, u"Soil"_s);
        m_server->blockEntities().container(at(2, 1, 2))->stacks[0] = soil;
        put(2, 0, 2, u"signal_hopper[facing=down]"_s);
        ticks(20);
        const QList<QcItemStack> &pulled = m_server->blockEntities().container(at(2, 0, 2))->stacks;
        QCOMPARE(pulled.at(0), soil);
        QVERIFY(m_server->blockEntities().container(at(2, 1, 2))->stacks.at(0).isEmpty());
    }

    void deathDropsKeepTheirData()
    {
        QcServerConfig config;
        config.keepInventory = false;
        join(config);
        const QcItemStack full = holding({named("qtcraft:dirt", 3, u"Soil"_s)});
        player().inventory.setSlot(0, full);
        player().inventory.setSlot(1, map(6));
        chat(u"/kill"_s);
        ticks(1);
        QCOMPARE(onTheGround(full), 1);
        QCOMPARE(onTheGround(map(6)), 1);
    }

    void creativeRequestsKeepOnlyTheMapId()
    {
        QcServerConfig config;
        config.defaultGameMode = QcGameMode::Creative;
        join(config);
        // Contents do not even decode from a client (test_item_data_limits::clientsCannotSendContents).
        send(QcMsg::CreativeItem{0, named("qtcraft:dirt", 3, u"Soil"_s)});
        send(QcMsg::CreativeItem{2, map(5).withName(u"Mine"_s)});
        QCOMPARE(player().inventory.slot(0), (QcItemStack{item("qtcraft:dirt"), 3, 0}));
        QCOMPARE(player().inventory.slot(2), map(5));
        QCOMPARE(m_client->stacks.value(2), map(5));
    }
};

QTEST_GUILESS_MAIN(TestItemDataPaths)
#include "test_item_data_paths.moc"
