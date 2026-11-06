// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/blockrules/QcPortalShapes.h"
#include "common/content/worldgen/QcPortalParser.h"
#include "common/physics/QcPhysics.h"
#include "fixtures/QcServerTestFixture.h"
#include "worldgen/structures/QcStructure.h"

#include <QJsonArray>
#include <QJsonObject>

using namespace Qt::StringLiterals;

namespace {

constexpr auto netherName = "qtcraft:nether";
constexpr auto portalName = "qtcraft:nether";

QcBlockPos cellOf(const QcVec3 &position)
{
    return QcPhysics::blockContaining(position);
}

} // namespace

// Portals from data (stage 83.3, decisions E-1020 to E-1025): the format, frames of obsidian lit by
// fire (also the completed ruins of the overworld), a broken frame putting the portal out, and a
// player's journey: the wait in survival, at once in creative, stepping out before going back and
// the way back through the portal he came by. Ticks are stepped by hand.
class TestPortals : public QcServerTestFixture
{
    Q_OBJECT

private:
    const QcPortalDefinition &nether() const { return *m_content->worldgen().portal(QString::fromLatin1(portalName)); }

    void set(QcBlockPos pos, const char *block) { QVERIFY(m_server->world().setBlock(pos, id(block))); }

    bool isPortal(QcBlockPos pos) const
    {
        const std::optional<QcBlockId> block = m_server->world().blockAt(pos);
        return block && m_content->block(*block).name == u"qtcraft:nether_portal"_s;
    }

    bool isFire(QcBlockPos pos) const { return m_server->world().blockAt(pos) == id("qtcraft:fire"); }

    // An upright frame of obsidian along x around `width` x `height` empty cells, the lowest of
    // them at `inner`; without its corners.
    void buildFrame(QcBlockPos inner, int width, int height)
    {
        for (int across = 0; across < width; ++across) {
            set(inner.offset(across, -1, 0), "qtcraft:obsidian");
            set(inner.offset(across, height, 0), "qtcraft:obsidian");
            for (int up = 0; up < height; ++up) {
                QVERIFY(m_server->world().setBlock(inner.offset(across, up, 0), qcAirBlockId));
            }
        }
        for (int up = 0; up < height; ++up) {
            set(inner.offset(-1, up, 0), "qtcraft:obsidian");
            set(inner.offset(width, up, 0), "qtcraft:obsidian");
        }
    }

    void holdFlintAndSteel()
    {
        chat(u"/give flint_and_steel 1"_s);
        ticks(1);
        const int slot = slotOf(item("qtcraft:flint_and_steel"));
        QVERIFY(slot >= 0 && slot < QcInventory::hotbarSize);
        send(QcMsg::SelectHotbarSlot{slot});
    }

    // Uses flint and steel on the top of the frame block under `inner`.
    void strike(QcBlockPos inner) { send(QcMsg::UseItem{inner.offset(0, -1, 0), QcBlockFace::Up}); }

    // The smallest frame two blocks in front of the player, lit.
    QcBlockPos litFrame()
    {
        const QcBlockPos inner = m_feet.offset(0, 0, 2);
        buildFrame(inner, 2, 3);
        holdFlintAndSteel();
        strike(inner);
        return inner;
    }

    void standIn(QcBlockPos inner)
    {
        chat(u"/tp %1 %2 %3"_s.arg(inner.x + 0.5).arg(inner.y).arg(inner.z + 0.5));
        ticks(1);
    }

    bool inNether() const { return m_client->dimensions.size() >= 2 && m_client->dimensions.last() == QString::fromLatin1(netherName); }

    QcServerDimension &netherDimension() { return dimension(QString::fromLatin1(netherName)); }
    QcServerDimension &overworld() { return dimension(QString::fromLatin1(QcDimensionDefinition::overworldName)); }

    QcServerDimension &dimension(const QString &name)
    {
        QcResult<QcServerDimension *> opened = m_server->openDimension(name);
        if (!opened) {
            qFatal("the dimension does not open: %s", qPrintable(opened.error()));
        }
        return **opened;
    }

    static QJsonObject portalJson()
    {
        return {{u"name"_s, u"test:gate"_s},
                {u"to"_s, u"test:elsewhere"_s},
                {u"block"_s, u"qtcraft:nether_portal"_s},
                {u"frame"_s, u"qtcraft:obsidian"_s},
                {u"shape"_s, QJsonObject{{u"type"_s, u"vertical"_s}, {u"min"_s, QJsonArray{4, 5}}, {u"max"_s, QJsonArray{8, 9}}}},
                {u"link"_s, QJsonObject{{u"type"_s, u"fixed"_s}}},
                {u"back"_s, u"spawn"_s},
                {u"wait"_s, QJsonObject{{u"survival"_s, 2.0}, {u"creative"_s, 0.0}}}};
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void parsesTheFormat()
    {
        const QcResult<QcPortalDefinition> portal = QcPortalParser::parsePortal(portalJson(), u"test"_s);
        QVERIFY2(portal, portal ? "" : qPrintable(portal.error()));
        QCOMPARE(portal->from, u"qtcraft:overworld"_s);
        QCOMPARE(portal->to, u"test:elsewhere"_s);
        QCOMPARE(portal->shape, QcPortalDefinition::Shape::Vertical);
        QCOMPARE(portal->maxWidth, 8);
        QCOMPARE(portal->link, QcPortalDefinition::Link::Fixed);
        QCOMPARE(portal->back, QcPortalDefinition::Back::Spawn);
        QCOMPARE(portal->survivalWait, 2.0);
        QCOMPARE(portal->trigger, QcPortalDefinition::Trigger::Fire);

        // The shipped portal: obsidian, 4 x 5 to 23 x 23, scaled with a platform.
        QCOMPARE(nether().frame, u"qtcraft:obsidian"_s);
        QCOMPARE(nether().minWidth, 4);
        QCOMPARE(nether().maxHeight, 23);
        QCOMPARE(nether().link, QcPortalDefinition::Link::Scaled);
        QCOMPARE(nether().platform, u"qtcraft:obsidian"_s);
    }

    void refusesBrokenPortals()
    {
        QJsonObject json = portalJson();
        json.remove(u"frame"_s);
        QVERIFY(!QcPortalParser::parsePortal(json, u"test"_s));

        json = portalJson();
        json[u"to"_s] = u"qtcraft:overworld"_s;
        QVERIFY(!QcPortalParser::parsePortal(json, u"test"_s));

        json = portalJson();
        json[u"shape"_s] = QJsonObject{{u"type"_s, u"vertical"_s}, {u"min"_s, QJsonArray{9, 5}}, {u"max"_s, QJsonArray{8, 9}}};
        QVERIFY(!QcPortalParser::parsePortal(json, u"test"_s));

        json = portalJson();
        json[u"shape"_s] = QJsonObject{{u"type"_s, u"ring"_s}, {u"min"_s, QJsonArray{5, 4}}, {u"max"_s, QJsonArray{5, 4}}};
        QVERIFY(!QcPortalParser::parsePortal(json, u"test"_s));
    }

    void findsFramesOfAllowedSizes()
    {
        join();
        const QcBlockPos high = m_feet.offset(-10, 6, 4);
        buildFrame(high, 21, 21);
        const std::optional<QcPortalFrame> largest = QcPortalShapes::find(m_server->world(), nether(), high.offset(7, 3, 0), QcPortalShapes::Inside::Empty);
        QVERIFY(largest);
        QCOMPARE(largest->origin, high);
        QCOMPARE(largest->width, 21);
        QCOMPARE(largest->height, 21);
        QCOMPARE(largest->axis, QcPortalFrame::Axis::X);

        // One block wider is too wide.
        buildFrame(high.offset(0, 0, 3), 22, 21);
        QVERIFY(!QcPortalShapes::find(m_server->world(), nether(), high.offset(5, 2, 3), QcPortalShapes::Inside::Empty));

        // Along z the same frame turned.
        const QcBlockPos turned = m_feet.offset(4, 0, -1);
        for (int up = -1; up <= 3; ++up) {
            set(turned.offset(0, up, -1), "qtcraft:obsidian");
            set(turned.offset(0, up, 2), "qtcraft:obsidian");
        }
        for (int up = 0; up <= 2; ++up) {
            QVERIFY(m_server->world().setBlock(turned.offset(0, up, 0), qcAirBlockId));
            QVERIFY(m_server->world().setBlock(turned.offset(0, up, 1), qcAirBlockId));
        }
        set(turned.offset(0, -1, 0), "qtcraft:obsidian");
        set(turned.offset(0, -1, 1), "qtcraft:obsidian");
        set(turned.offset(0, 3, 0), "qtcraft:obsidian");
        set(turned.offset(0, 3, 1), "qtcraft:obsidian");
        const std::optional<QcPortalFrame> alongZ = QcPortalShapes::find(m_server->world(), nether(), turned.offset(0, 1, 1), QcPortalShapes::Inside::Empty);
        QVERIFY(alongZ);
        QCOMPARE(alongZ->axis, QcPortalFrame::Axis::Z);
    }

    void fireLightsACompleteFrame()
    {
        join();
        const QcBlockPos inner = litFrame();
        for (int across = 0; across < 2; ++across) {
            for (int up = 0; up < 3; ++up) {
                QVERIFY(isPortal(inner.offset(across, up, 0)));
            }
        }
        const QcBlockDefinition &block = m_content->block(*m_server->world().blockAt(inner));
        QCOMPARE(block.lightLevel, 11);
        QCOMPARE(m_content->property(*m_server->world().blockAt(inner), u"axis"_s), u"x"_s);
        QCOMPARE(m_server->world().blockLight(inner.offset(0, 1, 0)), 11);
        QCOMPARE(overworld().portals().entries().size(), 1);
        QVERIFY(m_client->sounds.contains(u"qtcraft:block.portal.lit"_s));

        // The portal stays while its frame stands.
        ticks(40);
        QVERIFY(isPortal(inner.offset(1, 2, 0)));
    }

    void incompleteFramesMakeFire()
    {
        join();
        holdFlintAndSteel();

        // A side block missing.
        const QcBlockPos open = m_feet.offset(-2, 0, 2);
        buildFrame(open, 2, 3);
        QVERIFY(m_server->world().setBlock(open.offset(2, 1, 0), qcAirBlockId));
        strike(open);
        QVERIFY(isFire(open));
        QVERIFY(!isPortal(open));

        // Too narrow.
        const QcBlockPos narrow = m_feet.offset(2, 0, 2);
        buildFrame(narrow, 1, 3);
        strike(narrow);
        QVERIFY(isFire(narrow));
        QVERIFY(overworld().portals().entries().isEmpty());
    }

    void brokenFramePutsThePortalOut()
    {
        join();
        const QcBlockPos inner = litFrame();
        QVERIFY(isPortal(inner));
        const QcBlockPos bottom = inner.offset(1, -1, 0);
        chat(u"/setblock %1 %2 %3 air"_s.arg(bottom.x).arg(bottom.y).arg(bottom.z));
        ticks(5);
        for (int across = 0; across < 2; ++across) {
            for (int up = 0; up < 3; ++up) {
                QVERIFY2(!isPortal(inner.offset(across, up, 0)), qPrintable(u"%1 %2"_s.arg(across).arg(up)));
            }
        }
        // The index forgets it on its next look.
        ticks(QcServerPortals::forgetEveryTicks + 1);
        QVERIFY(overworld().portals().entries().isEmpty());
    }

    // The ruined portal of the overworld as the generator makes it, completed by hand: obsidian
    // where crying obsidian or nothing stands (decision E-1021); the generator output stays.
    void ruinedPortalLightsOnceComplete()
    {
        join();
        const QcStructureDefinition *definition = m_content->worldgen().structure(u"qtcraft:ruined_portal"_s);
        QVERIFY(definition);
        const QcStructure::Loaded ruin = QcStructure::fromDefinition(*definition, *m_content);
        QVERIFY(ruin);
        const QcBlockPos origin = m_feet.offset(-4, -2, 1);
        for (int x = 0; x < ruin->sizeX(); ++x) {
            for (int y = 0; y < ruin->sizeY(); ++y) {
                for (int z = 0; z < ruin->sizeZ(); ++z) {
                    if (const std::optional<QcBlockId> block = ruin->block(x, y, z)) {
                        QVERIFY(m_server->world().setBlock(origin.offset(x, y, z), *block));
                    }
                }
            }
        }
        chat(u"/tp %1 %2 %3"_s.arg(origin.x + 4.5).arg(origin.y + 2).arg(origin.z + 2.5));
        holdFlintAndSteel();
        const QcBlockPos inner = origin.offset(3, 2, 4);
        strike(inner.offset(1, 0, 0));
        QVERIFY(!isPortal(inner));
        QVERIFY(m_server->world().setBlock(inner.offset(1, 0, 0), qcAirBlockId));

        for (const QcBlockPos missing : {origin.offset(3, 1, 4), origin.offset(2, 3, 4), origin.offset(5, 4, 4), origin.offset(4, 5, 4)}) {
            set(missing, "qtcraft:obsidian");
        }
        strike(inner.offset(1, 0, 0));
        QVERIFY(isPortal(inner));
        QVERIFY(isPortal(inner.offset(1, 2, 0)));
    }

    void survivalWaitsFourSeconds()
    {
        join();
        const QcBlockPos inner = litFrame();
        standIn(inner);
        const int wait = static_cast<int>(std::ceil(nether().survivalWait / QcPhysics::tickDuration));
        idle(wait - 10);
        QVERIFY(!inNether());
        QVERIFY(m_server->systems().portals.travellers().ticksInside(m_client->entityId) > 0);
        idle(12);
        QVERIFY(inNether());

        // He arrives in a new portal on the ground of the biome, a quarter of a block from its middle.
        QcServerDimension &arrived = netherDimension();
        const QcBlockPos feet = cellOf(m_client->motion.position);
        const std::optional<QcBlockId> block = arrived.world().blockAt(feet);
        QVERIFY(block);
        QCOMPARE(m_content->block(*block).name, u"qtcraft:nether_portal"_s);
        QCOMPARE(arrived.world().blockAt(feet.offset(0, -1, 0)), id("qtcraft:obsidian"));
        const std::optional<QcBlockId> ground = arrived.world().blockAt(feet.offset(0, -2, 0));
        QVERIFY(ground && m_content->block(*ground).solid);
        QCOMPARE(arrived.portals().entries().size(), 1);
        QVERIFY(std::abs(m_client->motion.position.x - m_feet.x / 8.0) < 18.0);
    }

    void stepOutBeforeGoingBack()
    {
        join();
        const QcBlockPos inner = litFrame();
        chat(u"/gamemode creative"_s);
        standIn(inner);
        idle(2);
        QVERIFY(inNether());

        // Standing on in the arrival portal takes him nowhere.
        const QcVec3 arrival = m_client->motion.position;
        idle(120);
        QCOMPARE(m_client->dimensions.size(), 2);
        QVERIFY(m_server->systems().portals.travellers().mustStepOut(m_client->entityId));

        // Out and in again: back through the portal he came by, which is the only one.
        chat(u"/tp %1 %2 %3"_s.arg(arrival.x).arg(arrival.y).arg(arrival.z + 2.0));
        idle(2);
        chat(u"/tp %1 %2 %3"_s.arg(arrival.x).arg(arrival.y).arg(arrival.z));
        idle(2);
        QCOMPARE(m_client->dimensions.last(), u"qtcraft:overworld"_s);
        QVERIFY(isPortal(cellOf(m_client->motion.position)));
        QVERIFY((m_client->motion.position - QcVec3{inner.x + 1.0, static_cast<double>(inner.y), inner.z + 0.5}).length() < 1.0);
        QCOMPARE(overworld().portals().entries().size(), 1);
        QCOMPARE(netherDimension().portals().entries().size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestPortals)
#include "test_portals.moc"
