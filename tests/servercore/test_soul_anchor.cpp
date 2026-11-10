// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/blockrules/QcSafeStand.h"
#include "fixtures/QcServerTestFixture.h"
#include "fixtures/QcTestUtil.h"
#include "server/core/QcServerState.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>

using namespace Qt::StringLiterals;

// The soul anchor and the start in the nether (decisions E-1110 to E-1113): charging with
// glowstone, setting the respawn point in the nether, the respawn taking a charge, an empty,
// broken or blocked anchor sending the player to the world spawn, the blast outside the nether
// and of a bed inside it, the anchor kept in the player file, and a world that starts in the
// nether. Ticks are stepped by hand.
namespace {

const QString netherName = u"qtcraft:nether"_s;

} // namespace

class TestSoulAnchor : public QcServerTestFixture
{
    Q_OBJECT

private:
    // Where the stage in the nether lies: a room of air over netherrack, far from the spawn.
    static constexpr QcBlockPos netherFeet{200, 70, 200};

    QcServerDimension &nether()
    {
        QcResult<QcServerDimension *> opened = m_server->openDimension(netherName);
        if (!opened) {
            qFatal("the nether does not open: %s", qPrintable(opened.error()));
        }
        return **opened;
    }

    [[nodiscard]] bool inNether() const { return !m_client->dimensions.isEmpty() && m_client->dimensions.last() == netherName; }

    QcBlockId netherBlock(QcBlockPos pos) { return *nether().world().blockAt(pos); }

    void setInNether(QcBlockPos pos, const char *block) { QVERIFY(nether().world().setBlock(pos, id(block))); }

    // A room of 9 x 9 x 4 air over netherrack around netherFeet, and the player in its middle.
    void goToNether()
    {
        QcServerWorld &world = nether().world();
        for (int cx = -1; cx <= 1; ++cx) {
            for (int cz = -1; cz <= 1; ++cz) {
                world.loadChunk({netherFeet.chunk().x + cx, netherFeet.chunk().z + cz});
            }
        }
        for (int dx = -4; dx <= 4; ++dx) {
            for (int dz = -4; dz <= 4; ++dz) {
                QVERIFY(world.setBlock(netherFeet.offset(dx, -1, dz), id("qtcraft:netherrack")));
                for (int dy = 0; dy <= 3; ++dy) {
                    QVERIFY(world.setBlock(netherFeet.offset(dx, dy, dz), qcAirBlockId));
                }
            }
        }
        chat(u"/tp %1 %2 %3 in %4"_s.arg(netherFeet.x + 0.5).arg(netherFeet.y).arg(netherFeet.z + 0.5).arg(netherName));
        ticks(3);
        QVERIFY(inNether());
    }

    void hold(const char *name)
    {
        const int slot = slotOf(item(name));
        QVERIFY2(slot >= 0 && slot < QcInventory::hotbarSize, name);
        send(QcMsg::SelectHotbarSlot{slot});
        ticks(1);
    }

    // Selects an empty hotbar slot.
    void holdNothing()
    {
        send(QcMsg::SelectHotbarSlot{QcInventory::hotbarSize - 1});
        ticks(1);
    }

    void use(QcBlockPos pos)
    {
        send(QcMsg::UseItem{pos, QcBlockFace::Up});
        ticks(2);
    }

    [[nodiscard]] bool reached(const QString &advancement) const
    {
        return std::ranges::any_of(m_client->advancements, [&advancement](const QcMsg::AdvancementProgress &message) {
            return std::ranges::any_of(message.entries, [&advancement](const auto &entry) { return entry.advancement == advancement && entry.done; });
        });
    }

    [[nodiscard]] bool told(const QString &text) const { return m_client->chatLines.contains(text); }

    void dieAndRespawn()
    {
        chat(u"/kill"_s);
        ticks(2);
        QVERIFY(m_client->dead);
        send(QcMsg::Respawn{});
        ticks(3);
        QVERIFY(!m_client->dead);
    }

    [[nodiscard]] QcBlockPos feetCell() const { return QcPhysics::blockContaining(m_client->motion.position); }

    // An anchor with `charge` two blocks east of the player in the nether, bound with an empty hand.
    QcBlockPos boundAnchor(const char *block)
    {
        goToNether();
        const QcBlockPos anchor = netherFeet.offset(2, 0, 0);
        setInNether(anchor, block);
        holdNothing();
        use(anchor);
        return anchor;
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    // Glowstone raises the charge by one and is used up; a full anchor takes none.
    void glowstoneChargesTheAnchor()
    {
        join();
        goToNether();
        chat(u"/give glowstone 6"_s);
        hold("qtcraft:glowstone");
        const QcBlockPos anchor = netherFeet.offset(2, 0, 0);
        setInNether(anchor, "qtcraft:soul_anchor");
        for (int charge = 1; charge <= 4; ++charge) {
            use(anchor);
            QCOMPARE(m_content->block(netherBlock(anchor)).anchor.charge, charge);
            QCOMPARE(countOf(item("qtcraft:glowstone")), 6 - charge);
        }
        QVERIFY(reached(u"qtcraft:full_anchor"_s));

        // Full: glowstone sets the respawn point instead.
        use(anchor);
        QCOMPARE(netherBlock(anchor), id("qtcraft:soul_anchor_4"));
        QCOMPARE(countOf(item("qtcraft:glowstone")), 2);
        QVERIFY(told(u"Respawn point set"_s));
    }

    // The anchor's light rises with the charge, as in the reference.
    void theLightRisesWithTheCharge()
    {
        const QList<int> light{0, 3, 7, 11, 15};
        const QStringList names{u"qtcraft:soul_anchor"_s, u"qtcraft:soul_anchor_1"_s, u"qtcraft:soul_anchor_2"_s, u"qtcraft:soul_anchor_3"_s,
                                u"qtcraft:soul_anchor_4"_s};
        for (qsizetype charge = 0; charge < names.size(); ++charge) {
            const std::optional<QcBlockId> block = m_content->blockId(names.at(charge));
            QVERIFY(block);
            QCOMPARE(m_content->block(*block).lightLevel, light.at(charge));
            QCOMPARE(m_content->block(*block).anchor.charge, static_cast<int>(charge));
        }
    }

    // An empty anchor does nothing but take glowstone.
    void anEmptyAnchorSetsNothing()
    {
        join();
        const QcBlockPos anchor = boundAnchor("qtcraft:soul_anchor");
        QCOMPARE(netherBlock(anchor), id("qtcraft:soul_anchor"));
        QVERIFY(!told(u"Respawn point set"_s));
    }

    // A charged anchor in the nether sets the respawn point; each respawn there takes a charge and
    // puts the player beside it.
    void theRespawnTakesACharge()
    {
        join();
        const QcBlockPos anchor = boundAnchor("qtcraft:soul_anchor_2");
        QVERIFY(told(u"Respawn point set"_s));
        QVERIFY(reached(u"qtcraft:soul_anchor"_s));

        dieAndRespawn();
        QVERIFY(inNether());
        QCOMPARE(netherBlock(anchor), id("qtcraft:soul_anchor_1"));
        const QcBlockPos feet = feetCell();
        QVERIFY(std::abs(feet.x - anchor.x) <= 1 && std::abs(feet.z - anchor.z) <= 1 && std::abs(feet.y - anchor.y) <= 1);
        QVERIFY(feet != anchor);

        dieAndRespawn();
        QVERIFY(inNether());
        QCOMPARE(netherBlock(anchor), id("qtcraft:soul_anchor"));
    }

    // A respawn message from a living player (a double click, a spamming client) spends nothing.
    void aRespawnWhileAliveSpendsNothing()
    {
        join();
        const QcBlockPos anchor = boundAnchor("qtcraft:soul_anchor_2");
        send(QcMsg::Respawn{});
        ticks(3);
        QVERIFY(inNether());
        QCOMPARE(netherBlock(anchor), id("qtcraft:soul_anchor_2"));
        QVERIFY(!told(u"You have no charged soul anchor, or it was blocked"_s));
    }

    void twoRespawnsAfterOneDeathTakeOneCharge()
    {
        join();
        const QcBlockPos anchor = boundAnchor("qtcraft:soul_anchor_3");
        chat(u"/kill"_s);
        ticks(2);
        QVERIFY(m_client->dead);
        send(QcMsg::Respawn{});
        send(QcMsg::Respawn{});
        ticks(3);
        QVERIFY(!m_client->dead);
        QCOMPARE(netherBlock(anchor), id("qtcraft:soul_anchor_2"));
    }

    // Using the bound anchor again tells nothing new (the point did not change).
    void theSamePointIsToldOnce()
    {
        join();
        const QcBlockPos anchor = boundAnchor("qtcraft:soul_anchor_2");
        use(anchor);
        QCOMPARE(m_client->chatLines.count(u"Respawn point set"_s), 1);
    }

    // With the last charge gone the next respawn goes to the world spawn, and the anchor is forgotten.
    void anEmptiedAnchorSendsToTheWorldSpawn()
    {
        join();
        const QcBlockPos anchor = boundAnchor("qtcraft:soul_anchor_1");
        dieAndRespawn();
        QVERIFY(inNether());
        QCOMPARE(netherBlock(anchor), id("qtcraft:soul_anchor"));

        dieAndRespawn();
        QVERIFY(!inNether());
        QVERIFY(told(u"You have no charged soul anchor, or it was blocked"_s));
    }

    void aBrokenAnchorSendsToTheWorldSpawn()
    {
        join();
        const QcBlockPos anchor = boundAnchor("qtcraft:soul_anchor_3");
        QVERIFY(nether().world().setBlock(anchor, qcAirBlockId));
        dieAndRespawn();
        QVERIFY(!inNether());
    }

    // Walled in on every side and on top: no room beside it, and its charge stays.
    void aBlockedAnchorSendsToTheWorldSpawn()
    {
        join();
        const QcBlockPos anchor = boundAnchor("qtcraft:soul_anchor_3");
        chat(u"/tp %1 %2 %3"_s.arg(netherFeet.x - 3.5).arg(netherFeet.y).arg(netherFeet.z + 0.5));
        ticks(2);
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dy = -1; dy <= 2; ++dy) {
                    if (dx != 0 || dz != 0 || dy > 0) {
                        setInNether(anchor.offset(dx, dy, dz), "qtcraft:netherrack");
                    }
                }
            }
        }
        dieAndRespawn();
        QVERIFY(!inNether());
        QCOMPARE(netherBlock(anchor), id("qtcraft:soul_anchor_3"));
    }

    // Outside the nether a charged anchor explodes: the anchor goes, the ground under it breaks
    // and the player near it is hurt.
    void aChargedAnchorExplodesInTheOverworld()
    {
        join();
        const QcBlockPos anchor = m_feet.offset(3, 0, 0);
        QVERIFY(m_server->world().setBlock(anchor, id("qtcraft:soul_anchor_1")));
        holdNothing();
        const double before = m_client->health;
        use(anchor);
        QCOMPARE(*m_server->world().blockAt(anchor), qcAirBlockId);
        QVERIFY(!m_client->explosions.isEmpty());
        QCOMPARE(*m_server->world().blockAt(anchor.offset(0, -1, 0)), qcAirBlockId);
        QVERIFY(m_client->health < before);
        QVERIFY(!told(u"Respawn point set"_s));
    }

    // A bed used in the nether explodes, both halves with it.
    void aBedExplodesInTheNether()
    {
        join();
        goToNether();
        chat(u"/give bed 1"_s);
        hold("qtcraft:bed");
        const QcBlockPos ground = netherFeet.offset(3, -1, 0);
        use(ground);
        const QcBlockPos foot = ground.offset(0, 1, 0);
        QVERIFY(m_content->block(netherBlock(foot)).bed);
        holdNothing();
        use(foot);
        QVERIFY(!m_client->explosions.isEmpty());
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) {
                QVERIFY(!m_content->block(netherBlock(foot.offset(dx, 0, dz))).bed);
            }
        }
    }

    // The bound anchor is kept with the player (player file version 13).
    void theAnchorIsSaved()
    {
        QTemporaryDir dir;
        QcServerConfig config;
        config.worldDirectory = dir.path();
        join(config);
        const QcBlockPos anchor = boundAnchor("qtcraft:soul_anchor_2");
        QVERIFY(m_server->save());
        m_client.reset();
        m_server.reset();

        join(config);
        QVERIFY(inNether());
        // The new server has not loaded the anchor's chunk; the respawn does.
        dieAndRespawn();
        QVERIFY(inNether());
        QCOMPARE(netherBlock(anchor), id("qtcraft:soul_anchor_1"));
    }

    // A world that starts in the nether: the new player stands there safely, and without an anchor
    // he comes back there.
    void aWorldStartsInTheNether()
    {
        QcServerConfig config;
        config.startDimension = netherName;
        config.seed = 4711;
        join(config);
        QVERIFY(inNether());
        const QcBlockPos feet = feetCell();
        QVERIFY2(QcSafeStand::roomAt(nether().world(), feet), qPrintable(u"%1 %2 %3"_s.arg(feet.x).arg(feet.y).arg(feet.z)));

        dieAndRespawn();
        QVERIFY(inNether());
        QVERIFY(QcSafeStand::fits(nether().world(), feetCell()));
        QCOMPARE(feetCell(), feet);
    }

    // Who dies in the overworld of a world that starts in the nether comes back in the nether.
    void aDeathInTheOverworldOfANetherStartRespawnsInTheNether()
    {
        QcServerConfig config;
        config.startDimension = netherName;
        config.seed = 4711;
        join(config);
        const QcBlockPos start = feetCell();
        chat(u"/tp 0.5 120 0.5 in qtcraft:overworld"_s);
        ticks(3);
        QVERIFY(!inNether());

        dieAndRespawn();
        QVERIFY(inNether());
        QCOMPARE(feetCell(), start);
        QVERIFY(QcSafeStand::fits(nether().world(), feetCell()));
    }

    // A destroyed anchor in a world that starts in the nether falls back to the nether's spawn.
    void aDestroyedAnchorOfANetherStartFallsBackToTheNetherSpawn()
    {
        QcServerConfig config;
        config.startDimension = netherName;
        config.seed = 4711;
        join(config);
        const QcBlockPos start = feetCell();
        const QcBlockPos anchor = boundAnchor("qtcraft:soul_anchor_3");
        QVERIFY(nether().world().setBlock(anchor, qcAirBlockId));

        dieAndRespawn();
        QVERIFY(inNether());
        QVERIFY(told(u"You have no charged soul anchor, or it was blocked"_s));
        QCOMPARE(feetCell(), start);
        QVERIFY(QcSafeStand::fits(nether().world(), feetCell()));
    }

    // The start stays with the saved world, whatever a later start asks.
    void theStartIsSavedWithTheWorld()
    {
        QTemporaryDir dir;
        QcServerConfig config;
        config.worldDirectory = dir.path();
        config.startDimension = netherName;
        join(config);
        QVERIFY(m_server->save());
        m_client.reset();
        m_server.reset();

        QFile file(QDir(dir.path()).filePath(u"world.json"_s));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject generator = QJsonDocument::fromJson(file.readAll()).object().value(u"generator"_s).toObject();
        QCOMPARE(generator.value(u"startDimension"_s).toString(), netherName);
        file.close();

        // A new player of the reopened world starts there too.
        config.startDimension.clear();
        join(config);
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd), m_origin);
        RawClient other(std::move(clientEnd));
        other.hello(u"Other"_s);
        QcTestUtil::pump();
        ticks(2);
        QVERIFY(!other.dimensions.isEmpty());
        QCOMPARE(other.dimensions.last(), netherName);
    }

    // A dimension that does not allow a start leaves the world in the overworld.
    void anUnknownStartStaysInTheOverworld()
    {
        QcServerConfig config;
        config.startDimension = u"qtcraft:nowhere"_s;
        join(config);
        QVERIFY(!inNether());
    }
};

QTEST_GUILESS_MAIN(TestSoulAnchor)
#include "test_soul_anchor.moc"
