#include "clientcore/control/QcControlInterface.h"
#include "clientcore/session/QcClientSession.h"
#include "common/physics/QcPhysics.h"
#include "common/physics/QcRaycast.h"
#include "common/protocol/QcTransport.h"
#include "QcTestUtil.h"
#include "server/core/QcServerConfig.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"
#include "worlds/QcLocalServerHost.h"

#include <QBuffer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

constexpr int timeoutMs = 30000;

} // namespace

// End-to-end scenarios through the control interface: server thread, protocol, client session.
class TestScenarios : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<QcLocalServerHost> m_host;
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
                const QByteArray &data = m_output->data();
                const qsizetype newline = data.indexOf('\n', m_readOffset);
                if (newline < 0) {
                    return false;
                }
                response = QJsonDocument::fromJson(data.mid(m_readOffset, newline - m_readOffset)).object();
                m_readOffset = newline + 1;
                return true;
            },
            timeoutMs);

        if (!answered) {
            qWarning("no response to %s", QJsonDocument(command).toJson(QJsonDocument::Compact).constData());
        }
        return response;
    }

    static QcVec3 position(const QJsonObject &status)
    {
        const QJsonArray p = status.value(u"position"_s).toArray();
        return {p.at(0).toDouble(), p.at(1).toDouble(), p.at(2).toDouble()};
    }

    // Selects the hotbar slot holding `item` (given before).
    bool select(const QString &item)
    {
        const QJsonArray list = request({{u"cmd"_s, u"inventory"_s}}).value(u"slots"_s).toArray();
        for (const auto &value : list) {
            const QJsonObject slot = value.toObject();
            if (slot.value(u"item"_s).toString() == item && slot.value(u"slot"_s).toInt() < 9) {
                return request({{u"cmd"_s, u"select"_s}, {u"slot"_s, slot.value(u"slot"_s).toInt()}}).value(u"ok"_s).toBool();
            }
        }
        return false;
    }

    int countOf(const QString &item)
    {
        int total = 0;
        const QJsonArray list = request({{u"cmd"_s, u"inventory"_s}}).value(u"slots"_s).toArray();
        for (const auto &value : list) {
            const QJsonObject slot = value.toObject();
            total += slot.value(u"item"_s).toString() == item ? slot.value(u"count"_s).toInt() : 0;
        }
        return total;
    }

    // The single creature within eight metres as the client shows it, or nothing.
    std::optional<QJsonObject> onlyCreatureNearby()
    {
        const QJsonArray around = request({{u"cmd"_s, u"entities"_s}, {u"radius"_s, 8}}).value(u"entities"_s).toArray();
        return around.size() == 1 ? std::optional(around.first().toObject()) : std::nullopt;
    }

    // Waits until the client shows the creature still at one place for two ticks and returns it.
    std::optional<QJsonObject> settledCreature()
    {
        std::optional<QJsonObject> creature;
        const bool settled = QTest::qWaitFor(
            [&] {
                const std::optional<QJsonObject> before = onlyCreatureNearby();
                request({{u"cmd"_s, u"wait"_s}, {u"ticks"_s, 2}});
                creature = onlyCreatureNearby();
                return before && creature && before->value(u"position"_s) == creature->value(u"position"_s);
            },
            timeoutMs);
        return settled ? creature : std::nullopt;
    }

    // The ground cell beyond `body` (a body of `width` at `centre`) along `direction` whose column
    // the body does not reach, so a block may be placed above it.
    static QcBlockPos groundCellBeyond(const QcVec3 &centre, const QcVec3 &direction, double width, int groundY)
    {
        const double half = width / 2.0;
        for (double distance = 1.0;; distance += 0.25) {
            const QcVec3 at = centre + direction * distance;
            const int x = static_cast<int>(std::floor(at.x));
            const int z = static_cast<int>(std::floor(at.z));
            const bool apartX = x >= centre.x + half || x + 1 <= centre.x - half;
            const bool apartZ = z >= centre.z + half || z + 1 <= centre.z - half;
            if (apartX || apartZ) {
                return {x, groundY, z};
            }
        }
    }

private slots:
    void init()
    {
        std::shared_ptr<const QcContentRegistry> content = QcTestUtil::loadContent();
        QcServerConfig config;
        config.viewDistance = 2;
        config.worldName = u"Scenario"_s;
        m_host = std::make_unique<QcLocalServerHost>(config, content, std::make_unique<QcTestTerrainGenerator>(content));
        m_session = std::make_unique<QcClientSession>(m_host->connectClient(), u"Bot"_s);
        m_output = std::make_unique<QBuffer>();
        m_output->open(QIODevice::ReadWrite);
        m_control = std::make_unique<QcControlInterface>(m_session.get(), nullptr, m_output.get());
        m_readOffset = 0;
        m_session->start();
        const QJsonObject ready = request({{u"cmd"_s, u"waitUntilReady"_s}});
        QVERIFY2(ready.value(u"ok"_s).toBool(), QJsonDocument(ready).toJson().constData());
    }

    void cleanup()
    {
        m_control.reset();
        m_session.reset();
        QVERIFY(m_host->shutdown().has_value());
        m_host.reset();
    }

    void statusDescribesThePlayer()
    {
        const QJsonObject status = request({{u"cmd"_s, u"status"_s}, {u"id"_s, 7}});
        QCOMPARE(status.value(u"id"_s).toInt(), 7);
        QCOMPARE(status.value(u"state"_s).toString(), u"Playing"_s);
        QCOMPARE(status.value(u"world"_s).toString(), u"Scenario"_s);
        QVERIFY(status.value(u"onGround"_s).toBool());

        // Chunks are generated in the background; the full 5 x 5 square arrives shortly after.
        int loaded = 0;
        QVERIFY(QTest::qWaitFor(
            [&] {
                loaded = request({{u"cmd"_s, u"status"_s}}).value(u"loadedChunks"_s).toInt();
                return loaded == 25;
            },
            timeoutMs));
        QCOMPARE(loaded, 25);
    }

    void walkingSouthIncreasesZ()
    {
        const QcVec3 start = position(request({{u"cmd"_s, u"look"_s}, {u"yaw"_s, 180}}));
        const QJsonObject after = request({{u"cmd"_s, u"move"_s}, {u"forward"_s, 1}, {u"ticks"_s, 20}});
        QVERIFY(after.value(u"ok"_s).toBool());
        const QcVec3 end = position(after);
        QVERIFY2(end.z > start.z + 0.5, qPrintable(QString::number(end.z - start.z)));
        QVERIFY(std::abs(end.x - start.x) < 1e-9);
    }

    void jumpLeavesTheGroundAndLandsAgain()
    {
        const QJsonObject inAir = request({{u"cmd"_s, u"jump"_s}});
        QVERIFY(!inAir.value(u"onGround"_s).toBool());
        QVERIFY(position(inAir).y > 0.0);
        const QJsonObject landed = request({{u"cmd"_s, u"wait"_s}, {u"ticks"_s, 30}});
        QVERIFY(landed.value(u"onGround"_s).toBool());
    }

    void blockQueriesSeeSolidGroundBelowThePlayer()
    {
        const QcVec3 feet = position(request({{u"cmd"_s, u"status"_s}}));
        const QJsonObject below = request({{u"cmd"_s, u"block"_s},
                                           {u"x"_s, static_cast<int>(std::floor(feet.x))},
                                           {u"y"_s, static_cast<int>(std::floor(feet.y)) - 1},
                                           {u"z"_s, static_cast<int>(std::floor(feet.z))}});
        const QString name = below.value(u"block"_s).toString();
        QVERIFY2(name == u"qtcraft:grass_block"_s || name == u"qtcraft:sand"_s, qPrintable(name));
        const QJsonObject around = request({{u"cmd"_s, u"blocks"_s}, {u"radius"_s, 1}});
        QVERIFY(!around.value(u"blocks"_s).toArray().isEmpty());
    }

    void soundsCanBeObserved()
    {
        // Walking makes the player's own steps; they are reported with their ground and origin.
        request({{u"cmd"_s, u"look"_s}, {u"yaw"_s, 180}});
        request({{u"cmd"_s, u"move"_s}, {u"forward"_s, 1}, {u"ticks"_s, 30}});

        const auto keysOf = [](const QJsonObject &response, const QString &origin) {
            QStringList keys;
            const QJsonArray sounds = response.value(u"sounds"_s).toArray();
            for (const auto &sound : sounds) {
                if (sound.toObject().value(u"origin"_s).toString() == origin) {
                    keys.append(sound.toObject().value(u"key"_s).toString());
                }
            }
            return keys;
        };

        const QJsonObject walked = request({{u"cmd"_s, u"sounds"_s}, {u"clear"_s, true}});
        QVERIFY(walked.value(u"ok"_s).toBool());
        const QStringList steps = keysOf(walked, u"local"_s);
        QVERIFY2(steps.contains(u"qtcraft:block.grass.step"_s) || steps.contains(u"qtcraft:block.sand.step"_s), qPrintable(steps.join(u", "_s)));

        // Cleared: nothing new has happened since.
        QVERIFY(request({{u"cmd"_s, u"sounds"_s}}).value(u"sounds"_s).toArray().isEmpty());

        // Mining a block: local strikes while mining, the break from the server.
        request({{u"cmd"_s, u"chat"_s}, {u"text"_s, u"/setblock ~2 ~ ~ dirt"_s}});
        const QcVec3 feet = position(request({{u"cmd"_s, u"status"_s}}));
        const QJsonObject mined = request({{u"cmd"_s, u"mine"_s},
                                           {u"x"_s, static_cast<int>(std::floor(feet.x)) + 2},
                                           {u"y"_s, static_cast<int>(std::floor(feet.y))},
                                           {u"z"_s, static_cast<int>(std::floor(feet.z))}});
        QVERIFY2(mined.value(u"ok"_s).toBool(), QJsonDocument(mined).toJson().constData());
        request({{u"cmd"_s, u"wait"_s}, {u"ticks"_s, 2}});
        const QJsonObject heard = request({{u"cmd"_s, u"sounds"_s}});
        QVERIFY(keysOf(heard, u"local"_s).contains(u"qtcraft:block.dirt.hit"_s));
        QVERIFY(keysOf(heard, u"server"_s).contains(u"qtcraft:block.dirt.break"_s));

        const QJsonArray heardSounds = heard.value(u"sounds"_s).toArray();
        for (const auto &value : heardSounds) {
            const QJsonObject sound = value.toObject();
            if (sound.value(u"key"_s).toString() == u"qtcraft:block.dirt.break"_s) {
                QVERIFY(sound.value(u"known"_s).toBool());
                QVERIFY(sound.value(u"distance"_s).toDouble() < 4.0);
                // Close by and in view: loud, near the centre.
                QVERIFY(sound.value(u"gain"_s).toDouble() > 0.3);
                QVERIFY(std::abs(sound.value(u"pan"_s).toDouble()) < 0.3);
            }
        }

        // Under the open sky the wind fades in (at least the calm breeze of the ground, 0.12, E-876),
        // and there is no cave.
        const QJsonObject outside = request({{u"cmd"_s, u"wait"_s}, {u"ticks"_s, 60}});
        QVERIFY(outside.value(u"ok"_s).toBool());
        const QJsonObject ambience = request({{u"cmd"_s, u"sounds"_s}}).value(u"ambience"_s).toObject();
        QVERIFY2(ambience.value(u"wind"_s).toDouble() >= 0.1, QJsonDocument(ambience).toJson().constData());
        QCOMPARE(ambience.value(u"cave"_s).toDouble(), 0.0);
    }

    void bucketsAndBottlesThroughTheInterface()
    {
        // An empty bucket aims at liquids: using it on water fills it.
        request({{u"cmd"_s, u"chat"_s}, {u"text"_s, u"/fill ~1 ~ ~-1 ~3 ~3 ~1 air"_s}});
        request({{u"cmd"_s, u"chat"_s}, {u"text"_s, u"/setblock ~2 ~-1 ~ water"_s}});
        request({{u"cmd"_s, u"chat"_s}, {u"text"_s, u"/give bucket 1"_s}});
        QVERIFY(select(u"qtcraft:bucket"_s));

        const QcVec3 feet = position(request({{u"cmd"_s, u"status"_s}}));
        const QJsonObject used = request({{u"cmd"_s, u"use"_s},
                                          {u"x"_s, static_cast<int>(std::floor(feet.x)) + 2},
                                          {u"y"_s, static_cast<int>(std::floor(feet.y)) - 1},
                                          {u"z"_s, static_cast<int>(std::floor(feet.z))},
                                          {u"face"_s, u"up"_s}});
        QVERIFY2(used.value(u"ok"_s).toBool(), QJsonDocument(used).toJson().constData());
        request({{u"cmd"_s, u"wait"_s}, {u"ticks"_s, 2}});
        QCOMPARE(countOf(u"qtcraft:water_bucket"_s), 1);

        // A firefly at night: caught with a glass bottle.
        request({{u"cmd"_s, u"chat"_s}, {u"text"_s, u"/time set night"_s}});
        request({{u"cmd"_s, u"chat"_s}, {u"text"_s, u"/give glass_bottle 1"_s}});
        QVERIFY(select(u"qtcraft:glass_bottle"_s));
        request({{u"cmd"_s, u"look"_s}, {u"yaw"_s, 0}, {u"pitch"_s, 0}});
        request({{u"cmd"_s, u"chat"_s}, {u"text"_s, u"/summon firefly"_s}});
        request({{u"cmd"_s, u"wait"_s}, {u"ticks"_s, 2}});

        qint64 firefly = -1;
        const QJsonArray entities = request({{u"cmd"_s, u"entities"_s}, {u"radius"_s, 8}}).value(u"entities"_s).toArray();
        for (const auto &value : entities) {
            if (value.toObject().value(u"type"_s).toString() == u"qtcraft:firefly"_s) {
                firefly = value.toObject().value(u"id"_s).toInteger();
            }
        }

        QVERIFY(firefly >= 0);
        const QJsonObject caught = request({{u"cmd"_s, u"useEntity"_s}, {u"entity"_s, firefly}});
        QVERIFY2(caught.value(u"ok"_s).toBool(), QJsonDocument(caught).toJson().constData());
        QCOMPARE(countOf(u"qtcraft:firefly_bottle"_s), 1);
        QCOMPARE(countOf(u"qtcraft:glass_bottle"_s), 0);
    }

    // Fire as a bot plays it (decision E-393): `ignite` takes the flint and steel from the hotbar and
    // lights the face it is given; the status tells whether the player burns, and water puts it out.
    void fireThroughTheInterface()
    {
        request({{u"cmd"_s, u"chat"_s}, {u"text"_s, u"/fill ~1 ~ ~-1 ~3 ~3 ~1 air"_s}});
        const QJsonObject without = request({{u"cmd"_s, u"ignite"_s}, {u"x"_s, 0}, {u"y"_s, 0}, {u"z"_s, 0}});
        QVERIFY(!without.value(u"ok"_s).toBool());

        request({{u"cmd"_s, u"chat"_s}, {u"text"_s, u"/give flint_and_steel 1"_s}});
        QVERIFY(QTest::qWaitFor([&] { return countOf(u"qtcraft:flint_and_steel"_s) == 1; }, timeoutMs));
        const QcVec3 feet = position(request({{u"cmd"_s, u"status"_s}}));
        const int x = static_cast<int>(std::floor(feet.x)) + 2;
        const int y = static_cast<int>(std::floor(feet.y));
        const int z = static_cast<int>(std::floor(feet.z));
        const QJsonObject lit = request({{u"cmd"_s, u"ignite"_s}, {u"x"_s, x}, {u"y"_s, y - 1}, {u"z"_s, z}, {u"face"_s, u"up"_s}});
        QVERIFY2(lit.value(u"ok"_s).toBool(), QJsonDocument(lit).toJson().constData());
        request({{u"cmd"_s, u"wait"_s}, {u"ticks"_s, 2}});
        QCOMPARE(request({{u"cmd"_s, u"block"_s}, {u"x"_s, x}, {u"y"_s, y}, {u"z"_s, z}}).value(u"block"_s).toString(), u"qtcraft:fire"_s);

        // Standing in fire sets the player alight; water at the feet puts it out.
        request({{u"cmd"_s, u"chat"_s}, {u"text"_s, u"/setblock ~ ~ ~ fire"_s}});
        QVERIFY(QTest::qWaitFor([&] { return request({{u"cmd"_s, u"status"_s}}).value(u"burning"_s).toBool(); }, timeoutMs));
        request({{u"cmd"_s, u"chat"_s}, {u"text"_s, u"/setblock ~ ~ ~ water"_s}});
        QVERIFY(QTest::qWaitFor([&] { return !request({{u"cmd"_s, u"status"_s}}).value(u"burning"_s).toBool(); }, timeoutMs));
    }

    // Experience as a bot gets it (decision E-398): mine a diamond ore with a pickaxe, collect the
    // orbs it gives, check the level and points, then let an operator command add levels.
    void experienceThroughTheInterface()
    {
        request({{u"cmd"_s, u"chat"_s}, {u"text"_s, u"/give diamond_pickaxe 1"_s}});
        QVERIFY(QTest::qWaitFor([&] { return countOf(u"qtcraft:diamond_pickaxe"_s) == 1; }, timeoutMs));
        QVERIFY(select(u"qtcraft:diamond_pickaxe"_s));
        const QJsonObject before = request({{u"cmd"_s, u"status"_s}});
        QCOMPARE(before.value(u"experienceLevel"_s).toInt(), 0);
        QCOMPARE(before.value(u"experiencePoints"_s).toInt(), 0);
        QCOMPARE(before.value(u"experienceForNext"_s).toInt(), 7);

        const QcVec3 feet = position(before);
        const int x = static_cast<int>(std::floor(feet.x)) + 1;
        const int y = static_cast<int>(std::floor(feet.y));
        const int z = static_cast<int>(std::floor(feet.z));
        request({{u"cmd"_s, u"chat"_s}, {u"text"_s, u"/setblock ~1 ~ ~ diamond_ore"_s}});
        QVERIFY(QTest::qWaitFor([&] { return request({{u"cmd"_s, u"block"_s}, {u"x"_s, x}, {u"y"_s, y}, {u"z"_s, z}}).value(u"block"_s).toString() == u"qtcraft:diamond_ore"_s; }, timeoutMs));
        const QJsonObject mined = request({{u"cmd"_s, u"mine"_s}, {u"x"_s, x}, {u"y"_s, y}, {u"z"_s, z}});
        QVERIFY2(mined.value(u"ok"_s).toBool(), QJsonDocument(mined).toJson().constData());

        // The orbs fly to the bot: 3 to 7 points, so at least level 0 with 3 points.
        QJsonObject status;
        QVERIFY(QTest::qWaitFor(
            [&] {
                status = request({{u"cmd"_s, u"status"_s}});
                return status.value(u"experienceLevel"_s).toInt() > 0 || status.value(u"experiencePoints"_s).toInt() >= 3;
            },
            timeoutMs));
        const int total = status.value(u"experienceLevel"_s).toInt() * 7 + status.value(u"experiencePoints"_s).toInt();
        QVERIFY2(total >= 3 && total <= 7, QJsonDocument(status).toJson().constData());

        request({{u"cmd"_s, u"chat"_s}, {u"text"_s, u"/xp add Bot 30 levels"_s}});
        QVERIFY(QTest::qWaitFor([&] { return request({{u"cmd"_s, u"status"_s}}).value(u"experienceLevel"_s).toInt() >= 30; }, timeoutMs));
        const QJsonObject levelled = request({{u"cmd"_s, u"status"_s}});
        const int level = levelled.value(u"experienceLevel"_s).toInt();
        QCOMPARE(level, 30 + status.value(u"experienceLevel"_s).toInt());
        QCOMPARE(levelled.value(u"experienceForNext"_s).toInt(), 112 + (level - 30) * 9);
    }

    // Archery as a bot plays it (decision E-425): a bow and arrows given, a full draw at a wall,
    // the arrow sticks there, the bot walks over and picks it up again.
    void archeryThroughTheInterface()
    {
        const auto chat = [&](const QString &text) { request({{u"cmd"_s, u"chat"_s}, {u"text"_s, text}}); };
        chat(u"/fill ~-2 ~-1 ~-2 ~8 ~-1 ~2 stone"_s);
        chat(u"/fill ~-2 ~ ~-2 ~7 ~3 ~2 air"_s);
        chat(u"/fill ~8 ~ ~-2 ~8 ~3 ~2 stone"_s);
        chat(u"/give bow 1"_s);
        chat(u"/give arrow 2"_s);
        QVERIFY(QTest::qWaitFor([&] { return countOf(u"qtcraft:arrow"_s) == 2 && countOf(u"qtcraft:bow"_s) == 1; }, timeoutMs));

        // Yaw 90 looks east (+x), at the wall eight blocks away.
        const QJsonObject shot = request({{u"cmd"_s, u"shoot"_s}, {u"yaw"_s, 90.0}, {u"pitch"_s, 0.0}, {u"ticks"_s, 20}});
        QVERIFY2(shot.value(u"ok"_s).toBool(), QJsonDocument(shot).toJson().constData());
        QJsonObject stuck;
        QVERIFY(QTest::qWaitFor(
            [&] {
                const QJsonArray entities = request({{u"cmd"_s, u"entities"_s}, {u"radius"_s, 16}}).value(u"entities"_s).toArray();
                for (const auto &entity : entities) {
                    if (entity.toObject().value(u"type"_s).toString() == u"projectile"_s && entity.toObject().value(u"stuck"_s).toBool()) {
                        stuck = entity.toObject();
                        return true;
                    }
                }
                return false;
            },
            timeoutMs));
        QCOMPARE(stuck.value(u"item"_s).toString(), u"qtcraft:arrow"_s);
        QCOMPARE(countOf(u"qtcraft:arrow"_s), 1);

        // Walking to the wall picks it up again.
        request({{u"cmd"_s, u"move"_s}, {u"forward"_s, 1.0}, {u"ticks"_s, 60}});
        QVERIFY(QTest::qWaitFor([&] { return countOf(u"qtcraft:arrow"_s) == 2; }, timeoutMs));
    }

    // Trading as a bot plays it (decision E-414): a summoned villager made a smith, its screen
    // opened, an offer the inventory pays traded, the screen closed.
    void tradingThroughTheInterface()
    {
        const auto chat = [&](const QString &text) { request({{u"cmd"_s, u"chat"_s}, {u"text"_s, text}}); };
        chat(u"/fill ~-6 ~-1 ~-6 ~6 ~-1 ~6 stone"_s);
        chat(u"/fill ~-6 ~ ~-6 ~6 ~4 ~6 air"_s);
        // A glass ring keeps the villager close.
        chat(u"/fill ~-4 ~ ~-4 ~4 ~1 ~4 glass"_s);
        chat(u"/fill ~-3 ~ ~-3 ~3 ~1 ~3 air"_s);
        for (const char *give : {"/give coal 64", "/give iron_ingot 16", "/give emerald 16"}) {
            chat(QString::fromLatin1(give));
        }
        request({{u"cmd"_s, u"look"_s}, {u"yaw"_s, 0}, {u"pitch"_s, 0}});
        chat(u"/summon villager"_s);
        chat(u"/profession smith"_s);
        request({{u"cmd"_s, u"wait"_s}, {u"ticks"_s, 2}});

        qint64 villager = -1;
        const QJsonArray around = request({{u"cmd"_s, u"entities"_s}, {u"radius"_s, 8}}).value(u"entities"_s).toArray();
        for (const auto &value : around) {
            if (value.toObject().value(u"type"_s).toString() == u"qtcraft:villager"_s) {
                villager = value.toObject().value(u"id"_s).toInteger();
            }
        }
        QVERIFY(villager >= 0);

        const QJsonObject opened = request({{u"cmd"_s, u"openTrade"_s}, {u"entity"_s, villager}});
        QVERIFY2(opened.value(u"ok"_s).toBool() && opened.value(u"open"_s).toBool(), QJsonDocument(opened).toJson().constData());
        QCOMPARE(opened.value(u"profession"_s).toString(), u"profession.qtcraft.smith"_s);
        const QJsonArray offers = opened.value(u"offers"_s).toArray();
        QCOMPARE(offers.size(), 2);

        const auto affordable = std::ranges::find_if(offers, [](const QJsonValue &offer) { return offer.toObject().value(u"affordable"_s).toBool(); });
        QVERIFY(affordable != offers.end());
        const QJsonObject offer = affordable->toObject();
        const QString result = offer.value(u"result"_s).toObject().value(u"item"_s).toString();
        const int before = countOf(result);
        const QJsonObject traded = request({{u"cmd"_s, u"trade"_s}, {u"offer"_s, offer.value(u"index"_s)}});
        QVERIFY2(traded.value(u"ok"_s).toBool(), QJsonDocument(traded).toJson().constData());
        const QJsonObject after = traded.value(u"offers"_s).toArray().at(offer.value(u"index"_s).toInt()).toObject();
        QCOMPARE(after.value(u"uses"_s).toInt(), 1);
        QCOMPARE(traded.value(u"experience"_s).toInt(), 3);
        QCOMPARE(countOf(result), before + offer.value(u"result"_s).toObject().value(u"count"_s).toInt());

        const QJsonObject closed = request({{u"cmd"_s, u"closeTrade"_s}});
        QVERIFY(!closed.value(u"open"_s).toBool());
        QVERIFY(!request({{u"cmd"_s, u"trades"_s}}).value(u"open"_s).toBool());
    }

    // Farming as a bot plays it (decisions E-289 to E-293): till, plant, let a sped-up day ripen the
    // wheat, harvest, eat, and feed two pigs until a young one appears.
    void farmingThroughTheInterface()
    {
        const auto chat = [&](const QString &text) { request({{u"cmd"_s, u"chat"_s}, {u"text"_s, text}}); };
        chat(u"/fill ~-6 ~-1 ~-6 ~6 ~-1 ~6 grass_block"_s);
        chat(u"/fill ~-6 ~ ~-6 ~6 ~6 ~6 air"_s);
        chat(u"/setblock ~2 ~-1 ~1 water"_s);
        chat(u"/give wooden_hoe"_s);
        chat(u"/give wheat_seeds 1"_s);
        const QcVec3 feet = position(request({{u"cmd"_s, u"status"_s}}));
        const int x = static_cast<int>(std::floor(feet.x)) + 2;
        const int y = static_cast<int>(std::floor(feet.y)) - 1;
        const int z = static_cast<int>(std::floor(feet.z));
        const auto blockAt = [&](int dy) { return request({{u"cmd"_s, u"block"_s}, {u"x"_s, x}, {u"y"_s, y + dy}, {u"z"_s, z}}); };
        const auto useOn = [&](const QString &item) {
            QVERIFY(select(item));
            const QJsonObject used = request({{u"cmd"_s, u"use"_s}, {u"x"_s, x}, {u"y"_s, y}, {u"z"_s, z}, {u"face"_s, u"up"_s}});
            QVERIFY2(used.value(u"ok"_s).toBool(), QJsonDocument(used).toJson().constData());
            request({{u"cmd"_s, u"wait"_s}, {u"ticks"_s, 2}});
        };

        useOn(u"qtcraft:wooden_hoe"_s);
        QCOMPARE(blockAt(0).value(u"block"_s).toString(), u"qtcraft:farmland"_s);
        QCOMPARE(blockAt(0).value(u"state"_s).toObject().value(u"moist"_s).toString(), u"true"_s);
        useOn(u"qtcraft:wheat_seeds"_s);
        QCOMPARE(blockAt(1).value(u"state"_s).toObject().value(u"age"_s).toString(), u"0"_s);

        // Six game days (3600 s, three times the average) as fast as the server can, without nights
        // full of zombies.
        chat(u"/gamerule mobSpawning false"_s);
        chat(u"/gamerule daylightCycle false"_s);
        // The sprint reports its end with the ticks it ran; the wait only bounds a hanging server.
        chat(u"/tick sprint 6d"_s);
        const auto sprintEnded = [&] {
            const QJsonArray lines = request({{u"cmd"_s, u"chatLog"_s}}).value(u"lines"_s).toArray();
            return std::ranges::any_of(lines, [](const QJsonValue &line) { return line.toString().contains(u"Sprint ended: 72000 ticks"_s); });
        };
        QVERIFY(QTest::qWaitFor(sprintEnded, 10 * timeoutMs));
        QCOMPARE(blockAt(1).value(u"state"_s).toObject().value(u"age"_s).toString(), u"7"_s);
        const QJsonObject mined = request({{u"cmd"_s, u"mine"_s}, {u"x"_s, x}, {u"y"_s, y + 1}, {u"z"_s, z}});
        QVERIFY2(mined.value(u"ok"_s).toBool(), QJsonDocument(mined).toJson().constData());
        request({{u"cmd"_s, u"collect"_s}, {u"radius"_s, 4}, {u"maxTicks"_s, 100}});
        QCOMPARE(countOf(u"qtcraft:wheat"_s), 1);

        // Hungry players eat with "use".
        chat(u"/feed 10"_s);
        chat(u"/give bread 1"_s);
        QVERIFY(select(u"qtcraft:bread"_s));
        request({{u"cmd"_s, u"use"_s}});
        request({{u"cmd"_s, u"wait"_s}, {u"ticks"_s, 2}});
        QCOMPARE(request({{u"cmd"_s, u"status"_s}}).value(u"food"_s).toInt(), 15);

    }

    // Feeding animals as a bot: two pigs fed with carrots get a young one, a sheep takes wheat.
    void breedingThroughTheInterface()
    {
        const auto chat = [&](const QString &text) { request({{u"cmd"_s, u"chat"_s}, {u"text"_s, text}}); };
        chat(u"/fill ~-6 ~-1 ~-6 ~6 ~-1 ~6 grass_block"_s);
        chat(u"/fill ~-6 ~ ~-6 ~6 ~6 ~6 air"_s);
        chat(u"/gamerule mobSpawning false"_s);

        // Two pigs fed with carrots get a young one.
        chat(u"/give carrot 2"_s);
        // One pig west and one south of the player, each three metres away.
        for (const int yaw : {270, 180}) {
            request({{u"cmd"_s, u"look"_s}, {u"yaw"_s, yaw}, {u"pitch"_s, 0}});
            request({{u"cmd"_s, u"wait"_s}, {u"ticks"_s, 2}});
            chat(u"/summon pig"_s);
        }
        request({{u"cmd"_s, u"wait"_s}, {u"ticks"_s, 2}});
        QVERIFY(select(u"qtcraft:carrot"_s));
        const auto pigs = [&] {
            QJsonArray found;
            const QJsonArray entities = request({{u"cmd"_s, u"entities"_s}, {u"radius"_s, 6}}).value(u"entities"_s).toArray();
            for (const auto &value : entities) {
                if (value.toObject().value(u"type"_s).toString() == u"qtcraft:pig"_s) {
                    found.append(value);
                }
            }
            return found;
        };
        const QJsonArray parents = pigs();
        for (const auto &pig : parents) {
            const QJsonObject fed = request({{u"cmd"_s, u"useEntity"_s}, {u"entity"_s, pig.toObject().value(u"id"_s).toInteger()}});
            QVERIFY2(fed.value(u"ok"_s).toBool(), QJsonDocument(fed).toJson().constData());
        }
        // They court for a second before the young one comes.
        request({{u"cmd"_s, u"wait"_s}, {u"ticks"_s, 30}});
        const QJsonArray family = pigs();
        QCOMPARE(family.size(), 3);
        QCOMPARE(std::ranges::count_if(family, [](const QJsonValue &pig) { return pig.toObject().value(u"young"_s).toBool(); }), 1);

        // Wheat is no food for players, but the client knows sheep eat it (content sync).
        chat(u"/give wheat 1"_s);
        request({{u"cmd"_s, u"look"_s}, {u"yaw"_s, 0}, {u"pitch"_s, 0}});
        request({{u"cmd"_s, u"wait"_s}, {u"ticks"_s, 2}});
        chat(u"/summon sheep"_s);
        request({{u"cmd"_s, u"wait"_s}, {u"ticks"_s, 2}});
        QVERIFY(select(u"qtcraft:wheat"_s));
        const QJsonArray around = request({{u"cmd"_s, u"entities"_s}, {u"radius"_s, 6}}).value(u"entities"_s).toArray();
        const auto sheep = std::ranges::find_if(around, [](const QJsonValue &e) { return e.toObject().value(u"type"_s).toString() == u"qtcraft:sheep"_s; });
        QVERIFY(sheep != around.end());
        const QJsonObject fedSheep = request({{u"cmd"_s, u"useEntity"_s}, {u"entity"_s, (*sheep).toObject().value(u"id"_s).toInteger()}});
        QVERIFY2(fedSheep.value(u"ok"_s).toBool(), QJsonDocument(fedSheep).toJson().constData());
    }

    // Items that are no use on a creature act on the block behind it (decision E-293).
    void blocksArePlacedBehindACreature()
    {
        const auto chat = [&](const QString &text) { request({{u"cmd"_s, u"chat"_s}, {u"text"_s, text}}); };
        chat(u"/fill ~-6 ~-1 ~-6 ~6 ~-1 ~6 grass_block"_s);
        chat(u"/fill ~-6 ~ ~-6 ~6 ~6 ~6 air"_s);
        chat(u"/gamerule mobSpawning false"_s);
        request({{u"cmd"_s, u"look"_s}, {u"yaw"_s, 270}, {u"pitch"_s, 0}});
        chat(u"/give dirt 1"_s);
        QVERIFY(select(u"qtcraft:dirt"_s));
        // In a frozen world the pig gets exactly the ticks that are stepped, however slow the
        // machine (in real time a slow client gave it seconds to walk off). It lands within them
        // and may still take a step; the aimed cell below allows for that.
        chat(u"/tick freeze"_s);
        chat(u"/summon pig"_s);
        chat(u"/tick step 10"_s);
        const QcVec3 feet = position(request({{u"cmd"_s, u"status"_s}}));
        const auto landed = [&] {
            const std::optional<QJsonObject> creature = onlyCreatureNearby();
            return creature && position(*creature).y == feet.y;
        };
        QVERIFY(QTest::qWaitFor(landed, timeoutMs));
        const std::optional<QJsonObject> settled = settledCreature();
        QVERIFY(settled);

        // Aim at the ground beyond the pig, as seen from the player, in a column the pig's body
        // leaves free (a block is never placed into a creature).
        const QcVec3 pig = position(*settled);
        const std::shared_ptr<const QcContentRegistry> content = m_session->content();
        const double width = content->mobs()[*content->mobIndex(u"qtcraft:pig"_s)].width;
        const QcBlockPos ground = groundCellBeyond(pig, QcVec3{pig.x - feet.x, 0.0, pig.z - feet.z}.normalized(), width,
            static_cast<int>(std::floor(feet.y)) - 1);
        const QcVec3 face{ground.x + 0.5, ground.y + 1.0, ground.z + 0.5};
        QVERIFY((face - (feet + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0})).length() < QcRaycast::playerReach);
        const QJsonObject used = request({{u"cmd"_s, u"use"_s}, {u"x"_s, ground.x}, {u"y"_s, ground.y}, {u"z"_s, ground.z}, {u"face"_s, u"up"_s}});
        QVERIFY2(used.value(u"ok"_s).toBool(), QJsonDocument(used).toJson().constData());
        QCOMPARE(used.value(u"targetEntity"_s).toInteger(), settled->value(u"id"_s).toInteger());

        // The server's answer arrives when it arrives; under load that is many ticks later.
        const auto placed = [&] { return request({{u"cmd"_s, u"block"_s}, {u"x"_s, ground.x}, {u"y"_s, ground.y + 1}, {u"z"_s, ground.z}}).value(u"block"_s).toString(); };
        QVERIFY(QTest::qWaitFor([&] { return placed() == u"qtcraft:dirt"_s; }, timeoutMs));
        chat(u"/tick unfreeze"_s);
    }

    void viewModeCanBeSwitched()
    {
        QCOMPARE(request({{u"cmd"_s, u"view"_s}}).value(u"mode"_s).toString(), u"first"_s);
        QCOMPARE(request({{u"cmd"_s, u"view"_s}, {u"mode"_s, u"front"_s}}).value(u"mode"_s).toString(), u"front"_s);
        QCOMPARE(m_session->cameraMode(), QcCameraMode::Front);
        const QJsonObject wrong = request({{u"cmd"_s, u"view"_s}, {u"mode"_s, u"sideways"_s}});
        QCOMPARE(wrong.value(u"ok"_s).toBool(true), false);
        QCOMPARE(m_session->cameraMode(), QcCameraMode::Front);
        request({{u"cmd"_s, u"view"_s}, {u"mode"_s, u"first"_s}});
    }

    void errorsAreReportedNotFatal()
    {
        const QJsonObject unknown = request({{u"cmd"_s, u"fly"_s}, {u"id"_s, u"x"_s}});
        QCOMPARE(unknown.value(u"ok"_s).toBool(true), false);
        QCOMPARE(unknown.value(u"id"_s).toString(), u"x"_s);
        QCOMPARE(m_control->failedRequests(), 1);
        QVERIFY(request({{u"cmd"_s, u"status"_s}}).value(u"ok"_s).toBool());
    }

    void malformedLinesFromTheInputDeviceAreRejected()
    {
        QBuffer input;
        input.open(QIODevice::ReadWrite);
        QBuffer output;
        output.open(QIODevice::ReadWrite);
        QcControlInterface control(m_session.get(), &input, &output);
        input.write("not json\n{\"cmd\":\"status\"}\n");
        input.seek(0);
        emit input.readyRead();
        QTRY_VERIFY_WITH_TIMEOUT(output.data().count('\n') >= 2, timeoutMs);
        const QList<QByteArray> lines = output.data().split('\n');
        QCOMPARE(QJsonDocument::fromJson(lines.at(0)).object().value(u"ok"_s).toBool(true), false);
        QCOMPARE(QJsonDocument::fromJson(lines.at(1)).object().value(u"ok"_s).toBool(), true);
    }
};

QTEST_GUILESS_MAIN(TestScenarios)
#include "test_scenarios.moc"
