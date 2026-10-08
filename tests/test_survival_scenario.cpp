#include "QcBaseContent.h"
#include "QcClientSession.h"
#include "QcControlInterface.h"
#include "QcLocalServerHost.h"
#include "QcTerrainGenerator.h"
#include "QcTransport.h"

#include <QBuffer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

constexpr int timeoutMs = 60000;
// A seed whose spawn lies in a birch forest (see docs/screenshots/03-birch-forest-lighting.png).
constexpr qint64 forestSeed = 12345;

} // namespace

// The assignment's reference scenario, played only through the control interface, extended to a
// short playthrough: hunt, fight and eat; fell a tree -> craft planks -> build a tool -> mine a block
// with it; save, restart and find everything again.
class TestSurvivalScenario : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_worldDirectory;
    QcServerConfig m_config;
    std::shared_ptr<const QcContentRegistry> m_content;
    std::unique_ptr<QcLocalServerHost> m_host;
    // Where the crafting table was placed, checked again after reloading the world.
    QJsonObject m_table;
    std::unique_ptr<QcClientSession> m_session;
    std::unique_ptr<QBuffer> m_output;
    std::unique_ptr<QcControlInterface> m_control;
    qint64 m_readOffset = 0;

    QJsonObject request(const QJsonObject &command)
    {
        m_control->submit(command);
        QJsonObject response;
        const bool answered = QTest::qWaitFor(
            [&] {
                const qsizetype newline = m_output->data().indexOf('\n', m_readOffset);
                if (newline < 0) {
                    return false;
                }
                response = QJsonDocument::fromJson(m_output->data().mid(m_readOffset, newline - m_readOffset)).object();
                m_readOffset = newline + 1;
                return true;
            },
            timeoutMs);
        if (!answered) {
            qWarning("no answer to %s", QJsonDocument(command).toJson(QJsonDocument::Compact).constData());
        }
        return response;
    }

    static QcVec3 position(const QJsonObject &status)
    {
        const QJsonArray p = status.value(u"position"_s).toArray();
        return {p.at(0).toDouble(), p.at(1).toDouble(), p.at(2).toDouble()};
    }

    int countOf(const QString &item)
    {
        int total = 0;
        const QJsonArray stacks = request({{u"cmd"_s, u"inventory"_s}}).value(u"slots"_s).toArray();
        for (const auto &entry : stacks) {
            if (entry.toObject().value(u"item"_s).toString() == item) {
                total += entry.toObject().value(u"count"_s).toInt();
            }
        }
        return total;
    }

    int slotOf(const QString &item)
    {
        const QJsonArray stacks = request({{u"cmd"_s, u"inventory"_s}}).value(u"slots"_s).toArray();
        for (const auto &entry : stacks) {
            if (entry.toObject().value(u"item"_s).toString() == item) {
                return entry.toObject().value(u"slot"_s).toInt();
            }
        }
        return -1;
    }

    // Puts an item into the selected hotbar slot 0.
    void hold(const QString &item)
    {
        const int slot = slotOf(item);
        QVERIFY2(slot >= 0, qPrintable(item));
        if (slot != 0) {
            request({{u"cmd"_s, u"moveItem"_s}, {u"from"_s, slot}, {u"to"_s, 0}});
        }
        request({{u"cmd"_s, u"select"_s}, {u"slot"_s, 0}});
    }

    // Starts the server on the saved world and joins it through a fresh control interface.
    void joinWorld()
    {
        m_host = std::make_unique<QcLocalServerHost>(m_config, m_content,
                                                     std::make_unique<QcTerrainGenerator>(m_content, forestSeed));
        QVERIFY2(m_host->startError().isEmpty(), qPrintable(m_host->startError()));
        m_session = std::make_unique<QcClientSession>(m_host->connectClient(), u"Lumberjack"_s);
        m_output = std::make_unique<QBuffer>();
        m_output->open(QIODevice::ReadWrite);
        m_readOffset = 0;
        m_control = std::make_unique<QcControlInterface>(m_session.get(), nullptr, m_output.get());
        m_session->start();
        QVERIFY(request({{u"cmd"_s, u"waitUntilReady"_s}}).value(u"ok"_s).toBool());
    }

private slots:
    void initTestCase()
    {
        QStringList warnings;
        m_content = *QcBaseContent::load(warnings);
        m_config.viewDistance = 3;
        m_config.seed = forestSeed;
        m_config.worldName = u"Forest"_s;
        m_config.worldDirectory = m_worldDirectory.path();
        joinWorld();
    }

    void huntFightAndEat()
    {
        const auto nearest = [this](const QString &type) {
            const QJsonArray entities =
                request({{u"cmd"_s, u"entities"_s}, {u"radius"_s, 8}}).value(u"entities"_s).toArray();
            for (const auto &entry : entities) {
                if (entry.toObject().value(u"type"_s).toString() == type) {
                    return entry.toObject().value(u"id"_s).toInt();
                }
            }
            return -1;
        };
        const auto gone = [this](int id) {
            const QJsonArray entities =
                request({{u"cmd"_s, u"entities"_s}, {u"radius"_s, 64}}).value(u"entities"_s).toArray();
            return std::ranges::none_of(entities, [id](const QJsonValue &e) { return e.toObject().value(u"id"_s).toInt() == id; });
        };
        // The fight below needs a weapon; a stone sword is the one shortcut through a command.
        request({{u"cmd"_s, u"chat"_s}, {u"text"_s, u"/give stone_sword"_s}});
        hold(u"qtcraft:stone_sword"_s);

        // Hunt: a pig in reach is hit until it drops its meat.
        request({{u"cmd"_s, u"look"_s}, {u"yaw"_s, 0}, {u"pitch"_s, 0}});
        int pig = nearest(u"qtcraft:pig"_s);
        if (pig < 0) {
            request({{u"cmd"_s, u"chat"_s}, {u"text"_s, u"/summon pig"_s}});
            pig = nearest(u"qtcraft:pig"_s);
        }
        QVERIFY(pig >= 0);
        for (int round = 0; round < 40 && !gone(pig); ++round) {
            request({{u"cmd"_s, u"attack"_s}, {u"entity"_s, pig}});
            request({{u"cmd"_s, u"wait"_s}, {u"ticks"_s, 10}});
        }
        QVERIFY(gone(pig));
        QTRY_VERIFY_WITH_TIMEOUT(countOf(u"qtcraft:raw_porkchop"_s) >= 1, timeoutMs);

        // Fight: a zombie strikes back before it falls.
        request({{u"cmd"_s, u"chat"_s}, {u"text"_s, u"/summon zombie"_s}});
        const int zombie = nearest(u"qtcraft:zombie"_s);
        QVERIFY(zombie >= 0);
        double lowest = 20.0;
        for (int round = 0; round < 60 && !gone(zombie); ++round) {
            const QJsonObject status = request({{u"cmd"_s, u"attack"_s}, {u"entity"_s, zombie}});
            lowest = std::min(lowest, request({{u"cmd"_s, u"status"_s}}).value(u"health"_s).toDouble());
            if (!status.value(u"ok"_s).toBool()) {
                request({{u"cmd"_s, u"wait"_s}, {u"ticks"_s, 8}});
            }
        }
        QVERIFY(gone(zombie));
        QVERIFY(!request({{u"cmd"_s, u"status"_s}}).value(u"dead"_s).toBool());
        QVERIFY2(lowest < 20.0, "the zombie never hit the player");

        // Eat: the meat heals the wounds the zombie left (regeneration alone is slower).
        hold(u"qtcraft:raw_porkchop"_s);
        const double before = request({{u"cmd"_s, u"status"_s}}).value(u"health"_s).toDouble();
        const int meat = countOf(u"qtcraft:raw_porkchop"_s);
        QVERIFY2(before < 20.0, "regeneration healed the player before eating");
        request({{u"cmd"_s, u"use"_s}});
        QCOMPARE(countOf(u"qtcraft:raw_porkchop"_s), meat - 1);
        QVERIFY(request({{u"cmd"_s, u"status"_s}}).value(u"health"_s).toDouble() > before);
    }

    void cleanupTestCase()
    {
        m_control.reset();
        m_session.reset();
        QVERIFY(m_host->shutdown().has_value());
    }

    void attackReportsWhetherTheHitLanded()
    {
        // A pig appears three metres in front of the player; trees or hills may hide it, so try a few
        // directions. A hit right after a landed one is swallowed by the attack cooldown.
        bool tested = false;
        for (int yaw = 0; yaw < 360 && !tested; yaw += 45) {
            request({{u"cmd"_s, u"look"_s}, {u"yaw"_s, yaw}, {u"pitch"_s, 0}});
            request({{u"cmd"_s, u"chat"_s}, {u"text"_s, u"/summon pig"_s}});
            request({{u"cmd"_s, u"wait"_s}, {u"ticks"_s, 4}});
            const QJsonArray entities =
                request({{u"cmd"_s, u"entities"_s}, {u"radius"_s, 4}}).value(u"entities"_s).toArray();
            for (const auto &entry : entities) {
                const int pig = entry.toObject().value(u"id"_s).toInt();
                if (!request({{u"cmd"_s, u"attack"_s}, {u"entity"_s, pig}}).value(u"ok"_s).toBool()) {
                    continue;
                }
                const QJsonObject second = request({{u"cmd"_s, u"attack"_s}, {u"entity"_s, pig}});
                QVERIFY(!second.value(u"ok"_s).toBool());
                QVERIFY2(second.value(u"error"_s).toString().contains(u"did not land"_s),
                         qPrintable(second.value(u"error"_s).toString()));
                tested = true;
                break;
            }
        }
        QVERIFY(tested);
        QVERIFY(!request({{u"cmd"_s, u"attack"_s}, {u"entity"_s, 999999}}).value(u"ok"_s).toBool());
    }

    void fellATreeCraftAToolAndMineWithIt()
    {
        // 1. Find the nearest tree trunk and walk next to it.
        const QJsonArray blocks = request({{u"cmd"_s, u"blocks"_s}, {u"radius"_s, 8}}).value(u"blocks"_s).toArray();
        const QcVec3 start = position(request({{u"cmd"_s, u"status"_s}}));
        QJsonObject trunk;
        double nearest = 1e9;
        for (const auto &entry : blocks) {
            const QJsonObject block = entry.toObject();
            if (!block.value(u"block"_s).toString().endsWith(u"_log"_s)) {
                continue;
            }
            const double distance = std::hypot(block.value(u"x"_s).toInt() + 0.5 - start.x, block.value(u"z"_s).toInt() + 0.5 - start.z);
            if (distance < nearest) {
                nearest = distance;
                trunk = block;
            }
        }
        QVERIFY2(!trunk.isEmpty(), "no tree near the spawn");
        const QString log = trunk.value(u"block"_s).toString();
        const int trunkX = trunk.value(u"x"_s).toInt();
        const int trunkZ = trunk.value(u"z"_s).toInt();
        for (int step = 0; step < 6; ++step) {
            const QcVec3 here = position(request({{u"cmd"_s, u"status"_s}}));
            const double dx = trunkX + 0.5 - here.x;
            const double dz = trunkZ + 0.5 - here.z;
            if (std::hypot(dx, dz) < 2.2) {
                break;
            }
            request({{u"cmd"_s, u"look"_s}, {u"yaw"_s, qRadiansToDegrees(std::atan2(dx, -dz))}, {u"pitch"_s, 0}});
            request({{u"cmd"_s, u"move"_s}, {u"forward"_s, 1}, {u"jump"_s, true}, {u"ticks"_s, 6}});
            request({{u"cmd"_s, u"wait"_s}, {u"ticks"_s, 10}});
        }

        // 2. Chop three logs of the trunk, lowest first.
        int chopped = 0;
        for (int y = 40; y < 140 && chopped < 3; ++y) {
            const QJsonObject block = request({{u"cmd"_s, u"block"_s}, {u"x"_s, trunkX}, {u"y"_s, y}, {u"z"_s, trunkZ}});
            if (block.value(u"block"_s).toString() != log) {
                continue;
            }
            const QJsonObject mined = request({{u"cmd"_s, u"mine"_s}, {u"x"_s, trunkX}, {u"y"_s, y}, {u"z"_s, trunkZ}});
            QVERIFY2(mined.value(u"ok"_s).toBool(), qPrintable(mined.value(u"error"_s).toString()));
            ++chopped;
        }
        QCOMPARE(chopped, 3);
        QTRY_COMPARE_WITH_TIMEOUT(countOf(log), 3, timeoutMs);

        // 3. Planks: one log through the 2 x 2 grid, the rest from the recipe book; then sticks and a
        //    crafting table.
        int logSlot = -1;
        const QJsonArray stacks = request({{u"cmd"_s, u"inventory"_s}}).value(u"slots"_s).toArray();
        for (const auto &entry : stacks) {
            if (entry.toObject().value(u"item"_s).toString() == log) {
                logSlot = entry.toObject().value(u"slot"_s).toInt();
            }
        }
        QVERIFY(logSlot >= 0);
        request({{u"cmd"_s, u"moveItem"_s}, {u"from"_s, logSlot}, {u"to"_s, 36}, {u"count"_s, 2}});
        const QJsonObject grid = request({{u"cmd"_s, u"inventory"_s}});
        QCOMPARE(grid.value(u"craftingResult"_s).toObject().value(u"item"_s).toString(), u"qtcraft:oak_planks"_s);
        QVERIFY(request({{u"cmd"_s, u"takeResult"_s}}).value(u"ok"_s).toBool());
        QCOMPARE(countOf(u"qtcraft:oak_planks"_s), 4);
        // Closing returns the second log from the grid.
        QVERIFY(request({{u"cmd"_s, u"closeCrafting"_s}}).value(u"ok"_s).toBool());
        QCOMPARE(countOf(log), 2);
        const QString planksRecipe = u"qtcraft:oak_planks_from_"_s + log.section(u':', 1);
        request({{u"cmd"_s, u"craft"_s}, {u"recipe"_s, planksRecipe}, {u"count"_s, 2}});
        QCOMPARE(countOf(u"qtcraft:oak_planks"_s), 12);
        request({{u"cmd"_s, u"craft"_s}, {u"recipe"_s, u"qtcraft:crafting_table"_s}});
        request({{u"cmd"_s, u"craft"_s}, {u"recipe"_s, u"qtcraft:stick"_s}});
        QCOMPARE(countOf(u"qtcraft:crafting_table"_s), 1);
        QCOMPARE(countOf(u"qtcraft:stick"_s), 4);
        QCOMPARE(countOf(u"qtcraft:oak_planks"_s), 6);

        // Without a crafting table nearby a 3 x 3 recipe is refused.
        const QJsonObject refused = request({{u"cmd"_s, u"craft"_s}, {u"recipe"_s, u"qtcraft:wooden_pickaxe"_s}});
        QVERIFY(refused.value(u"messages"_s).toArray().first().toString().contains(u"crafting table"_s));
        QCOMPARE(countOf(u"qtcraft:wooden_pickaxe"_s), 0);

        // 4. Place the table on the ground next to the player and craft a pickaxe there.
        hold(u"qtcraft:crafting_table"_s);
        const QcVec3 here = position(request({{u"cmd"_s, u"status"_s}}));
        const int feetY = int(std::floor(here.y));
        bool placed = false;
        for (const auto &[ox, oz] : {std::pair{2, 0}, std::pair{-2, 0}, std::pair{0, 2}, std::pair{0, -2}}) {
            const int x = int(std::floor(here.x)) + ox;
            const int z = int(std::floor(here.z)) + oz;
            request({{u"cmd"_s, u"place"_s}, {u"x"_s, x}, {u"y"_s, feetY - 1}, {u"z"_s, z}, {u"face"_s, u"up"_s}});
            if (request({{u"cmd"_s, u"block"_s}, {u"x"_s, x}, {u"y"_s, feetY}, {u"z"_s, z}}).value(u"block"_s).toString()
                == u"qtcraft:crafting_table"_s) {
                placed = true;
                m_table = {{u"cmd"_s, u"block"_s}, {u"x"_s, x}, {u"y"_s, feetY}, {u"z"_s, z}};
                break;
            }
        }
        QVERIFY(placed);
        request({{u"cmd"_s, u"craft"_s}, {u"recipe"_s, u"qtcraft:wooden_pickaxe"_s}});
        QCOMPARE(countOf(u"qtcraft:wooden_pickaxe"_s), 1);
        QCOMPARE(countOf(u"qtcraft:oak_planks"_s), 3);
        QCOMPARE(countOf(u"qtcraft:stick"_s), 2);

        // 5. Dig straight down with the pickaxe until stone yields cobblestone.
        hold(u"qtcraft:wooden_pickaxe"_s);
        for (int depth = 0; depth < 12 && countOf(u"qtcraft:cobblestone"_s) == 0; ++depth) {
            // The player is 0.6 m wide and may stand on up to four blocks; dig all of them.
            const QcVec3 feet = position(request({{u"cmd"_s, u"status"_s}}));
            const int y = int(std::floor(feet.y)) - 1;
            for (int x = int(std::floor(feet.x - 0.3)); x <= int(std::floor(feet.x + 0.3)); ++x) {
                for (int z = int(std::floor(feet.z - 0.3)); z <= int(std::floor(feet.z + 0.3)); ++z) {
                    if (request({{u"cmd"_s, u"block"_s}, {u"x"_s, x}, {u"y"_s, y}, {u"z"_s, z}}).value(u"block"_s).toString()
                        == u"qtcraft:air"_s) {
                        continue;
                    }
                    const QJsonObject mined = request({{u"cmd"_s, u"mine"_s}, {u"x"_s, x}, {u"y"_s, y}, {u"z"_s, z}});
                    QVERIFY2(mined.value(u"ok"_s).toBool(), qPrintable(mined.value(u"error"_s).toString()));
                }
            }
            request({{u"cmd"_s, u"wait"_s}, {u"ticks"_s, 12}});
        }
        QVERIFY(countOf(u"qtcraft:cobblestone"_s) >= 1);
        const QJsonObject held = request({{u"cmd"_s, u"status"_s}}).value(u"held"_s).toObject();
        QCOMPARE(held.value(u"item"_s).toString(), u"qtcraft:wooden_pickaxe"_s);
        QVERIFY(held.value(u"damage"_s).toInt() >= 1);
    }

    void savedWorldComesBackAfterARestart()
    {
        const QJsonObject before = request({{u"cmd"_s, u"status"_s}});
        const int pickaxeDamage = before.value(u"held"_s).toObject().value(u"damage"_s).toInt();
        QVERIFY(request({{u"cmd"_s, u"chat"_s}, {u"text"_s, u"/save"_s}}).value(u"ok"_s).toBool());
        m_control.reset();
        m_session.reset();
        QVERIFY(m_host->shutdown().has_value());
        m_host.reset();

        joinWorld();
        const QJsonObject after = request({{u"cmd"_s, u"status"_s}});
        const QcVec3 was = position(before);
        const QcVec3 is = position(after);
        QVERIFY2(std::abs(was.x - is.x) < 0.01 && std::abs(was.y - is.y) < 0.01 && std::abs(was.z - is.z) < 0.01,
                 QJsonDocument(after).toJson(QJsonDocument::Compact).constData());
        QCOMPARE(after.value(u"health"_s).toDouble(), before.value(u"health"_s).toDouble());
        QCOMPARE(countOf(u"qtcraft:wooden_pickaxe"_s), 1);
        QCOMPARE(after.value(u"held"_s).toObject().value(u"damage"_s).toInt(), pickaxeDamage);
        QVERIFY(countOf(u"qtcraft:cobblestone"_s) >= 1);
        QCOMPARE(request(m_table).value(u"block"_s).toString(), u"qtcraft:crafting_table"_s);
    }
};

QTEST_GUILESS_MAIN(TestSurvivalScenario)
#include "test_survival_scenario.moc"
