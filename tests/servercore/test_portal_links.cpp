// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/worldgen/QcDimensionParser.h"
#include "common/content/worldgen/QcPortalParser.h"
#include "common/physics/QcPhysics.h"
#include "fixtures/QcServerTestFixture.h"
#include "server/dimensions/QcPortalIndex.h"

#include <QJsonObject>

using namespace Qt::StringLiterals;

namespace {

constexpr auto netherName = "qtcraft:nether";
constexpr auto voidName = "test:void";
constexpr auto endName = "test:end";

// The base content with a dimension parsed from `json`, and the nether portal leading there by
// `link` ("scaled" or "fixed") and back to the spawn (fixed) or the portal it came by.
std::shared_ptr<QcContentRegistry> contentLeadingTo(const QJsonObject &json, const QString &link)
{
    QcResult<QcDimensionDefinition> dimension = QcDimensionParser::parseDimension(json, u"test"_s);
    if (!dimension) {
        qFatal("test dimension does not parse: %s", qPrintable(dimension.error()));
    }
    const QString name = dimension->name;
    std::shared_ptr<QcContentRegistry> content = QcTestUtil::loadContent();
    content->worldgen().registerDimension(std::move(*dimension));

    const QJsonObject portal{{u"name"_s, u"qtcraft:nether"_s},
                             {u"to"_s, name},
                             {u"block"_s, u"qtcraft:nether_portal"_s},
                             {u"frame"_s, u"qtcraft:obsidian"_s},
                             {u"link"_s, QJsonObject{{u"type"_s, link}, {u"platform"_s, u"qtcraft:obsidian"_s}}},
                             {u"back"_s, link == u"fixed"_s ? u"spawn"_s : u"linked"_s}};
    QcResult<QcPortalDefinition> parsed = QcPortalParser::parsePortal(portal, u"qtcraft"_s);
    if (!parsed) {
        qFatal("test portal does not parse: %s", qPrintable(parsed.error()));
    }
    content->worldgen().registerPortal(std::move(*parsed));
    return content;
}

} // namespace

// Where portals lead and what goes through them (stage 83.3, decisions E-1023 to E-1025):
// creatures, items and minecarts at once, then resting; a new portal on a floor of obsidian over
// nothing; the fixed arrival of an end-like dimension, which leads one way, and its way back to
// the spawn; the index of portals saved and read back. Ticks are stepped by hand.
class TestPortalLinks : public QcServerTestFixture
{
    Q_OBJECT

private:
    QcBlockPos m_inner;

    QcServerDimension &dimension(const QString &name)
    {
        QcResult<QcServerDimension *> opened = m_server->openDimension(name);
        if (!opened) {
            qFatal("the dimension does not open: %s", qPrintable(opened.error()));
        }
        return **opened;
    }

    QcServerDimension &nether() { return dimension(QString::fromLatin1(netherName)); }

    bool isPortal(const QcServerWorld &world, QcBlockPos pos) const
    {
        const std::optional<QcBlockId> block = world.blockAt(pos);
        return block && m_content->block(*block).name == u"qtcraft:nether_portal"_s;
    }

    // A creative player and a lit portal of 2 x 3 in front of him, lit by fire.
    void startWithPortal()
    {
        join();
        chat(u"/gamemode creative"_s);
        chat(u"/gamerule mobSpawning false"_s);
        m_inner = m_feet.offset(0, 0, 3);
        for (int across = -1; across <= 2; ++across) {
            QVERIFY(m_server->world().setBlock(m_inner.offset(across, -1, 0), id("qtcraft:obsidian")));
            QVERIFY(m_server->world().setBlock(m_inner.offset(across, 3, 0), id("qtcraft:obsidian")));
        }
        for (int up = 0; up < 3; ++up) {
            QVERIFY(m_server->world().setBlock(m_inner.offset(-1, up, 0), id("qtcraft:obsidian")));
            QVERIFY(m_server->world().setBlock(m_inner.offset(2, up, 0), id("qtcraft:obsidian")));
        }
        QVERIFY(m_server->systems().portals.lightByFire(m_inner.offset(1, 1, 0)));
        QVERIFY(isPortal(m_server->world(), m_inner));
    }

    QcVec3 middle() const { return {m_inner.x + 1.0, m_inner.y + 0.2, m_inner.z + 0.5}; }

    void standIn(QcBlockPos inner)
    {
        chat(u"/tp %1 %2 %3"_s.arg(inner.x + 0.5).arg(inner.y).arg(inner.z + 0.5));
        idle(2);
    }

    // Whether the item `id` lies in `dimension`.
    static bool holdsItem(const QcServerDimension &dimension, quint32 id)
    {
        return std::ranges::any_of(dimension.entities().items().items(), [id](const QcItemEntity &item) { return item.id == id; });
    }

    quint32 dropDirt()
    {
        QcItemEntityManager::Drop drop;
        drop.stack = QcItemStack{item("qtcraft:dirt"), 1, 0};
        drop.position = middle();
        return m_server->items().spawn(drop, m_server->entities().newId());
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
        m_content = QcTestUtil::loadContent();
    }

    void creaturesItemsAndMinecartsTravel()
    {
        startWithPortal();
        const quint32 pig = m_server->mobs().spawn(*m_content->mobKind(u"qtcraft:pig"_s), middle(), m_server->entities().newId());
        const quint32 dirt = dropDirt();
        QcMinecart cart = m_server->carts().fromItem(u"qtcraft:minecart"_s);
        cart.id = m_server->entities().newId();
        cart.motion.position = middle();
        QVERIFY(m_server->carts().add(cart));
        QVERIFY(pig != 0 && dirt != 0);

        ticks(2);
        QcServerDimension &there = nether();
        QVERIFY(m_server->mobs().find(pig) == nullptr);
        QVERIFY(there.entities().creatures().find(pig) != nullptr);
        QVERIFY(!holdsItem(dimension(u"qtcraft:overworld"_s), dirt));
        QVERIFY(holdsItem(there, dirt));
        QVERIFY(m_server->carts().find(cart.id) == nullptr);
        QVERIFY(there.entities().minecarts().find(cart.id) != nullptr);
        // One portal for all of them, built where the first arrived.
        QCOMPARE(there.portals().entries().size(), 1);
    }

    void travellersRestBeforeGoingBack()
    {
        startWithPortal();
        const quint32 dirt = dropDirt();
        ticks(2);
        QVERIFY(holdsItem(nether(), dirt));

        // It lies in the arrival portal and rests there for its cooldown.
        const int cooldown = static_cast<int>(std::ceil(m_content->worldgen().portal(u"qtcraft:nether"_s)->cooldown / QcPhysics::tickDuration));
        ticks(cooldown - 20);
        QVERIFY(holdsItem(nether(), dirt));
        ticks(40);
        QVERIFY(holdsItem(dimension(u"qtcraft:overworld"_s), dirt));
    }

    // Into a dimension of nothing: the new portal stands on a floor of obsidian with room to step
    // out on both sides (decision E-1025).
    void newPortalOverNothingGetsAFloor()
    {
        m_content = contentLeadingTo({{u"name"_s, QString::fromLatin1(voidName)}, {u"generator"_s, QJsonObject{{u"type"_s, u"void"_s}, {u"options"_s, u"radius=0"_s}}}, {u"weather"_s, false}}, u"scaled"_s);
        startWithPortal();
        standIn(m_inner);
        QCOMPARE(m_client->dimensions.last(), QString::fromLatin1(voidName));

        QcServerDimension &there = dimension(QString::fromLatin1(voidName));
        const QcBlockPos feet = QcPhysics::blockContaining(m_client->motion.position);
        QVERIFY(isPortal(there.world(), feet));
        QCOMPARE(there.world().blockAt(feet.offset(0, -1, 0)), id("qtcraft:obsidian"));
        for (const int side : {-1, 1}) {
            QCOMPARE(there.world().blockAt(feet.offset(0, -1, side)), id("qtcraft:obsidian"));
            QCOMPARE(there.world().blockAt(feet.offset(0, 0, side)), qcAirBlockId);
            QCOMPARE(there.world().blockAt(feet.offset(0, 2, side)), qcAirBlockId);
        }
        // At the same place: the void dimension has the overworld's scale.
        QVERIFY(std::abs(m_client->motion.position.x - middle().x) < 1.0);
        QVERIFY(std::abs(m_client->motion.position.z - middle().z) < 1.0);
    }

    // Through the portal into a dimension of nothing with stone in it: a ledge one block wide along
    // x at the point, and a field to step out on `fieldAt` blocks along x away (none with 0).
    // A portal from low down does not come out in the lava sea of a dimension (portalFloor).
    void portalFloorKeepsTheFrameUp()
    {
        m_content = contentLeadingTo({{u"name"_s, QString::fromLatin1(voidName)}, {u"portalFloor"_s, 150}, {u"generator"_s, QJsonObject{{u"type"_s, u"void"_s}, {u"options"_s, u"radius=0"_s}}}, {u"weather"_s, false}}, u"scaled"_s);
        startWithPortal();
        standIn(m_inner);
        QCOMPARE(m_client->dimensions.last(), QString::fromLatin1(voidName));
        QVERIFY(QcPhysics::blockContaining(m_client->motion.position).y >= 150);
    }

    void travelOverGround(int fieldAt)
    {
        m_content = contentLeadingTo({{u"name"_s, QString::fromLatin1(voidName)}, {u"generator"_s, QJsonObject{{u"type"_s, u"void"_s}, {u"options"_s, u"radius=0"_s}}}, {u"weather"_s, false}}, u"scaled"_s);
        startWithPortal();
        QcServerDimension &there = dimension(QString::fromLatin1(voidName));
        const QcBlockPos ledge = m_inner.offset(0, -2, 0);
        for (int dx = -6; dx <= fieldAt + 6; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) {
                const bool field = fieldAt > 0 && dx >= fieldAt - 3;
                if (dz == 0 || field) {
                    there.world().loadChunk(ledge.offset(dx, 0, dz).chunk());
                    QVERIFY(there.world().setBlock(ledge.offset(dx, 0, dz), id("qtcraft:stone")));
                }
            }
        }
        standIn(m_inner);
        QCOMPARE(m_client->dimensions.last(), QString::fromLatin1(voidName));
    }

    // A frame on a ledge one block wide, stepping out drops (as in the role model): the ledge
    // wins over a floor of obsidian when nothing better is near (decision E-1025).
    void aLedgeTakesThePortalWhenNothingBetterIsNear()
    {
        travelOverGround(0);
        QcServerDimension &there = dimension(QString::fromLatin1(voidName));
        const QcBlockPos feet = QcPhysics::blockContaining(m_client->motion.position);
        QVERIFY(isPortal(there.world(), feet));
        QCOMPARE(there.world().blockAt(feet.offset(0, -2, 0)), id("qtcraft:stone"));
        for (const int side : {-1, 1}) {
            QCOMPARE(there.world().blockAt(feet.offset(0, -1, side)), qcAirBlockId);
        }
    }

    // Ground in front of and behind the frame wins over a nearer ledge (decision E-1025).
    void groundToStepOutOnWinsOverALedge()
    {
        travelOverGround(8);
        QcServerDimension &there = dimension(QString::fromLatin1(voidName));
        const QcBlockPos feet = QcPhysics::blockContaining(m_client->motion.position);
        QVERIFY(isPortal(there.world(), feet));
        QVERIFY(feet.x >= m_inner.x + 5);
        for (const int side : {-1, 1}) {
            QCOMPARE(there.world().blockAt(feet.offset(0, -2, side)), id("qtcraft:stone"));
            QCOMPARE(there.world().blockAt(feet.offset(0, -1, side)), qcAirBlockId);
        }
    }

    // The search for ground looks through the whole column: a floor far above the point is found.
    void groundFarAboveIsFound()
    {
        m_content = contentLeadingTo({{u"name"_s, QString::fromLatin1(voidName)}, {u"generator"_s, QJsonObject{{u"type"_s, u"void"_s}, {u"options"_s, u"platform=qtcraft:stone,radius=16,y=240"_s}}}, {u"weather"_s, false}}, u"scaled"_s);
        startWithPortal();
        QVERIFY(std::abs(m_inner.x) < 10 && std::abs(m_inner.z) < 10);
        standIn(m_inner);
        QCOMPARE(m_client->dimensions.last(), QString::fromLatin1(voidName));
        QcServerDimension &there = dimension(QString::fromLatin1(voidName));
        const QcBlockPos feet = QcPhysics::blockContaining(m_client->motion.position);
        QCOMPARE(feet.y, 242);
        QVERIFY(isPortal(there.world(), feet));
        QCOMPARE(there.world().blockAt(feet.offset(0, -2, 0)), id("qtcraft:stone"));
    }

    // An end-like dimension with a fixed arrival: a platform there, no portal, so the way leads
    // one way; a portal lit there leads back to the spawn of the overworld.
    void fixedArrivalLeadsOneWay()
    {
        const QJsonObject arrival{{u"x"_s, 40}, {u"y"_s, 50}, {u"z"_s, -20}, {u"platform"_s, u"qtcraft:obsidian"_s}, {u"platformRadius"_s, 2}};
        m_content = contentLeadingTo({{u"name"_s, QString::fromLatin1(endName)}, {u"generator"_s, QJsonObject{{u"type"_s, u"void"_s}}}, {u"weather"_s, false}, {u"arrival"_s, arrival}}, u"fixed"_s);
        startWithPortal();
        standIn(m_inner);
        QCOMPARE(m_client->dimensions.last(), QString::fromLatin1(endName));
        QVERIFY((m_client->motion.position - QcVec3{40.5, 50.0, -19.5}).length() < 0.5);

        QcServerDimension &end = dimension(QString::fromLatin1(endName));
        for (int dx = -2; dx <= 2; ++dx) {
            for (int dz = -2; dz <= 2; ++dz) {
                QCOMPARE(end.world().blockAt({40 + dx, 49, -20 + dz}), id("qtcraft:obsidian"));
            }
        }
        QVERIFY(end.portals().entries().isEmpty());

        // A frame lit in the end, on its platform: back to the spawn.
        const QcBlockPos inner{39, 50, -18};
        for (int across = -1; across <= 2; ++across) {
            QVERIFY(end.world().setBlock(inner.offset(across, -1, 0), id("qtcraft:obsidian")));
            QVERIFY(end.world().setBlock(inner.offset(across, 3, 0), id("qtcraft:obsidian")));
        }
        for (int up = 0; up < 3; ++up) {
            QVERIFY(end.world().setBlock(inner.offset(-1, up, 0), id("qtcraft:obsidian")));
            QVERIFY(end.world().setBlock(inner.offset(2, up, 0), id("qtcraft:obsidian")));
        }
        chat(u"/give flint_and_steel 1"_s);
        ticks(1);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:flint_and_steel"))});
        send(QcMsg::UseItem{inner.offset(0, -1, 0), QcBlockFace::Up});
        QVERIFY(isPortal(end.world(), inner));
        standIn(inner);
        QCOMPARE(m_client->dimensions.last(), u"qtcraft:overworld"_s);
        QVERIFY((m_client->motion.position - QcVec3{m_feet.x + 0.5, static_cast<double>(m_feet.y), m_feet.z + 0.5}).length() < 3.0);
    }

    void indexIsSavedAndReadBack()
    {
        QcPortalIndex index;
        const QcPortalFrame frame{{3, 70, -9}, QcPortalFrame::Axis::Z, 2, 3};
        index.add({u"qtcraft:nether"_s, frame});
        index.add({u"qtcraft:nether"_s, {{100, 64, 4}, QcPortalFrame::Axis::X, 4, 5}});
        // The same frame twice is one entry.
        index.add({u"qtcraft:nether"_s, frame});
        QCOMPARE(index.entries().size(), 2);

        const QcResult<QList<QcPortalIndex::Entry>> entries = QcPortalIndex::fromBytes(index.bytes());
        QVERIFY(entries);
        QCOMPARE(*entries, index.entries());
        QcPortalIndex read;
        read.restore(*entries);
        const QList<QcPortalIndex::Entry> near = read.near(u"qtcraft:nether"_s, {4.0, 70.0, -8.0}, 16.0);
        QCOMPARE(near.size(), 1);
        QCOMPARE(near.first().frame, frame);
        QVERIFY(read.near(u"other:gate"_s, {4.0, 70.0, -8.0}, 16.0).isEmpty());
        QCOMPARE(read.holding({3, 71, -8}), std::optional(near.first()));
        QVERIFY(!QcPortalIndex::fromBytes("not json"));
    }
};

QTEST_GUILESS_MAIN(TestPortalLinks)
#include "test_portal_links.moc"
