// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/player/QcClientVitals.h"
#include "common/gameplay/QcAirSupply.h"
#include "common/protocol/QcProtocol.h"
#include "fixtures/QcServerTestFixture.h"
#include "server/commands/QcGameRules.h"
#include "server/gameplay/QcDrowning.h"
#include "server/storage/QcPlayerStorage.h"
#include "server/storage/QcSaveFile.h"
#include "server/storage/QcWorldStorage.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;

// Air under water and drowning (decision E-955), ticked by hand: the air supply on its own (drain,
// the rhythm of the hits, refill, bubbles), a player in a closed water cell (air, damage, death
// message, the few PlayerAir messages, refill in the air, a full breath after the respawn), feet
// in shallow water, creative and spectator players and the game rule drowningDamage off (no
// damage), the rule's round trip in world.json, the player file round trip and older files with
// a full breath, and creatures: a pig drowns and leaves its loot; a drowned, a cod, undead and
// golems do not, nor does a small creature at the surface. The message on the wire and what the
// client makes of it (the HUD's properties, the pop of a lost bubble, none on joining).
class TestDrowning : public QcServerTestFixture
{
    Q_OBJECT

private:
    QcServerClient &player() { return *m_server->entities().clients().front(); }

    // Closes the cell column at `cell` and the one above it in glass and fills both with water,
    // under a glass ceiling: whatever stands in it keeps its eyes under water.
    void waterColumn(QcBlockPos cell)
    {
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dy = 0; dy <= 2; ++dy) {
                    m_server->world().setBlock(cell.offset(dx, dy, dz), id("qtcraft:glass"));
                }
            }
        }
        m_server->world().setBlock(cell, id("qtcraft:water"));
        m_server->world().setBlock(cell.offset(0, 1, 0), id("qtcraft:water"));
    }

    // Takes the water of the column at `cell` away again.
    void drain(QcBlockPos cell)
    {
        m_server->world().setBlock(cell, qcAirBlockId);
        m_server->world().setBlock(cell.offset(0, 1, 0), qcAirBlockId);
    }

    // A creature of `name` with its feet at `cell`, without a mind (it sinks), kept with the world.
    quint32 creatureAt(const QString &name, QcBlockPos cell)
    {
        const int type = *m_content->mobIndex(name);
        const QcVec3 at{cell.x + 0.5, static_cast<double>(cell.y), cell.z + 0.5};
        const quint32 mob = m_server->mobs().spawn(type, at, m_server->entities().newId(), false, true);
        m_server->mobs().find(mob)->noAi = true;
        return mob;
    }

    QString lastLine() const { return m_client->chatLines.isEmpty() ? QString() : m_client->chatLines.constLast(); }

private slots:
    void airSupplyDrainsHitsAndRefills()
    {
        QcAirSupply air;
        QCOMPARE(air.air(), QcAirSupply::maxAir);
        QCOMPARE(QcAirSupply::maxAir, 300);

        // Fifteen seconds of breath, one tick at a time.
        for (int i = 0; i < 300; ++i) {
            QVERIFY(!air.tick(true));
        }
        QCOMPARE(air.air(), 0);

        // The first hit a second after the last breath, then one every second.
        for (int i = 0; i < 19; ++i) {
            QVERIFY(!air.tick(true));
        }
        QVERIFY(air.tick(true));
        for (int i = 0; i < 19; ++i) {
            QVERIFY(!air.tick(true));
        }
        QVERIFY(air.tick(true));

        // Four back in each tick out of the water: full again after 75 ticks; surfacing restarts
        // the rhythm.
        QVERIFY(!air.tick(false));
        QCOMPARE(air.air(), 4);
        for (int i = 0; i < 74; ++i) {
            QVERIFY(!air.tick(false));
        }
        QCOMPARE(air.air(), 300);
        QVERIFY(!air.tick(false));
        QCOMPARE(air.air(), 300);

        // Saves are clamped; reset fills up.
        air.restore(-5);
        QCOMPARE(air.air(), 0);
        air.restore(1000);
        QCOMPARE(air.air(), 300);
        air.restore(7);
        air.reset();
        QCOMPARE(air.air(), 300);
    }

    void bubblesCountStartedTenths()
    {
        QCOMPARE(QcAirSupply::bubblesOf(300), 10);
        QCOMPARE(QcAirSupply::bubblesOf(271), 10);
        QCOMPARE(QcAirSupply::bubblesOf(270), 9);
        QCOMPARE(QcAirSupply::bubblesOf(1), 1);
        QCOMPARE(QcAirSupply::bubblesOf(0), 0);
        QCOMPARE(QcAirSupply::bubblesOf(-3), 0);
        QCOMPARE(QcAirSupply::bubblesOf(400), 10);
        QCOMPARE(QcAirSupply::bubblesOf(5, 10), 5);
        QCOMPARE(QcAirSupply::bubblesOf(5, 0), 0);

        // A message goes out only when the HUD would change.
        const QcMsg::PlayerAir full{300, 300, false};
        QVERIFY(!QcDrowning::differs(full, full));
        QVERIFY(QcDrowning::differs(full, {300, 300, true}));
        QVERIFY(QcDrowning::differs(full, {299, 300, false}));
        QVERIFY(!QcDrowning::differs({299, 300, true}, {280, 300, true}));
        QVERIFY(QcDrowning::differs({271, 300, true}, {270, 300, true}));
    }

    void airMessageRoundTrip()
    {
        const auto decoded = QcProtocol::decode(QcProtocol::encode(QcMsg::PlayerAir{120, 300, true}), QcProtocol::Peer::Server);
        QVERIFY(decoded.has_value());
        const auto &air = std::get<QcMsg::PlayerAir>(*decoded);
        QCOMPARE(air.air, 120);
        QCOMPARE(air.maxAir, 300);
        QVERIFY(air.submerged);

        // Out of range, and never from a client.
        for (const QcMsg::PlayerAir bad : {QcMsg::PlayerAir{301, 300, false}, QcMsg::PlayerAir{-1, 300, true}, QcMsg::PlayerAir{0, 0, true}}) {
            QVERIFY(!QcProtocol::decode(QcProtocol::encode(bad), QcProtocol::Peer::Server).has_value());
        }
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMsg::PlayerAir{1, 300, false}), QcProtocol::Peer::Client).has_value());
    }

    void clientShowsBubblesWhileUnderWaterOrRefilling()
    {
        QcClientVitals vitals;
        QVERIFY(!vitals.airShown());
        QCOMPARE(vitals.airBubbles(), 10);
        QCOMPARE(vitals.air(), 1.0);

        // Under water with a full breath: shown, nothing popped yet.
        QCOMPARE(vitals.apply(QcMsg::PlayerAir{300, 300, true}), QcClientVitals::Change::Air);
        QVERIFY(vitals.airShown());
        QVERIFY(!vitals.bubblePopped());

        // A bubble goes: it pops.
        vitals.apply(QcMsg::PlayerAir{270, 300, true});
        QCOMPARE(vitals.airBubbles(), 9);
        QCOMPARE(vitals.air(), 0.9);
        QVERIFY(vitals.bubblePopped());

        // Surfacing: still shown while refilling, no pop when bubbles come back.
        vitals.apply(QcMsg::PlayerAir{274, 300, false});
        QVERIFY(vitals.airShown());
        QVERIFY(!vitals.bubblePopped());
        vitals.apply(QcMsg::PlayerAir{300, 300, false});
        QVERIFY(!vitals.airShown());
        QCOMPARE(vitals.apply(QcMsg::PlayerFood{20, 20}), QcClientVitals::Change::Food);
    }

    void firstAirAfterJoiningPopsNothing()
    {
        // Joining under water with air already lost tells the state; no bubble went just now.
        QcClientVitals vitals;
        vitals.apply(QcMsg::PlayerAir{248, 300, true});
        QCOMPARE(vitals.airBubbles(), 9);
        QVERIFY(!vitals.bubblePopped());
        vitals.apply(QcMsg::PlayerAir{240, 300, true});
        QVERIFY(vitals.bubblePopped());
    }

    void playerDrownsInWater()
    {
        join();
        waterColumn(m_feet);
        ticks(1);
        QVERIFY(!m_client->airMessages.isEmpty());
        QVERIFY(m_client->airMessages.constLast().submerged);

        // The breath lasts 300 ticks; then a hit of one heart every second.
        ticks(299);
        QCOMPARE(player().air.air(), 0);
        QCOMPARE(player().health, 20.0);
        // Few messages: one per lost bubble and the change under water, not one per tick.
        QVERIFY2(m_client->airMessages.size() <= 12, qPrintable(QString::number(m_client->airMessages.size())));
        QCOMPARE(m_client->airMessages.constLast().air, 0);
        ticks(20);
        QCOMPARE(player().health, 18.0);
        ticks(20);
        QCOMPARE(player().health, 16.0);

        // Down to death, with its own message.
        for (int i = 0; i < 400 && !player().dead; ++i) {
            ticks(1);
        }
        QVERIFY(player().dead);
        QCOMPARE(lastLine(), u"Raw drowned"_s);
        QCOMPARE(m_client->chatKeys.constLast().key, u"%1 drowned"_s);
    }

    void airRefillsOutOfTheWater()
    {
        join();
        waterColumn(m_feet);
        ticks(100);
        QCOMPARE(player().air.air(), 200);
        drain(m_feet);
        ticks(1);
        QCOMPARE(player().air.air(), 204);
        QVERIFY(!m_client->airMessages.constLast().submerged);
        ticks(24);
        QCOMPARE(player().air.air(), 300);
        // The full breath is told, so the HUD hides the bubbles.
        QCOMPARE(m_client->airMessages.constLast().air, 300);
        QCOMPARE(player().health, 20.0);
    }

    void feetInWaterBreathe()
    {
        // Standing in a source one block deep and in flowing water the eyes stay above the
        // surface: the air stays full.
        join();
        m_server->world().setBlock(m_feet, id("qtcraft:water"));
        ticks(50);
        QCOMPARE(player().air.air(), QcAirSupply::maxAir);
        const std::optional<QcBlockId> flowing = m_content->stateId(u"qtcraft:water[level=3]"_s);
        QVERIFY(flowing);
        m_server->world().setBlock(m_feet, *flowing);
        ticks(1);
        QCOMPARE(player().air.air(), QcAirSupply::maxAir);
        QVERIFY(m_client->airMessages.isEmpty() || !m_client->airMessages.constLast().submerged);
    }

    void respawnGivesAFullBreath()
    {
        join();
        waterColumn(m_feet);
        for (int i = 0; i < 800 && !player().dead; ++i) {
            ticks(1);
        }
        QVERIFY(player().dead);
        QVERIFY(player().air.air() < QcAirSupply::maxAir);
        drain(m_feet);

        // A dead body refills by 4 a tick; the respawn fills the breath at once and tells it.
        send(QcMsg::Respawn{});
        ticks(1);
        QVERIFY(!player().dead);
        QCOMPARE(player().air.air(), QcAirSupply::maxAir);
        QCOMPARE(m_client->airMessages.constLast().air, QcAirSupply::maxAir);
        QVERIFY(!m_client->airMessages.constLast().submerged);
    }

    void creativeAndSpectatorPlayersBreathe()
    {
        join();
        waterColumn(m_feet);
        chat(u"/gamemode creative"_s);
        ticks(400);
        QCOMPARE(player().air.air(), 300);
        QCOMPARE(player().health, 20.0);

        chat(u"/gamemode spectator"_s);
        ticks(400);
        QCOMPARE(player().air.air(), 300);

        // Back in survival the air runs out again.
        chat(u"/gamemode survival"_s);
        ticks(10);
        QCOMPARE(player().air.air(), 290);
    }

    void gameRuleOffKeepsTheDamageAway()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QcServerConfig config;
        config.worldDirectory = directory.path();
        join(config);
        QVERIFY(QcGameRules::find(u"drowningDamage"));
        chat(u"/gamerule drowningDamage"_s);
        QCOMPARE(lastLine(), u"drowningDamage is true"_s);
        chat(u"/gamerule drowningDamage false"_s);
        QCOMPARE(lastLine(), u"drowningDamage is now false"_s);

        // The air still runs out, nobody is hurt.
        waterColumn(m_feet);
        ticks(400);
        QCOMPARE(player().air.air(), 0);
        QCOMPARE(player().health, 20.0);

        // Saved in world.json.
        QVERIFY(m_server->save().has_value());
        QFile file(QDir(directory.path()).filePath(u"world.json"_s));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject json = QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(json.value(u"gameRules"_s).toObject().value(u"drowningDamage"_s).toBool(true), false);
        const QcResult<QcWorldInfo> info = QcWorldStorage(directory.path(), m_content).readInfo();
        QVERIFY(info.has_value());
        QVERIFY(!info->drowningDamage);
    }

    void airIsSavedWithThePlayer()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QcPlayerStorage storage(directory.path(), m_content);
        QcPlayerData data;
        data.motion.position = {0.5, 70.0, 0.5};
        data.air = 123;
        const QcPlayerStorage::Result read = storage.parse(u"Raw"_s, storage.bytes(data));
        QVERIFY(read && read->player);
        QCOMPARE(read->player->air, 123);

        // A file of version 9 has no air: the player comes back with a full breath.
        const QcResult<QcSaveFile::Payload> payload = QcSaveFile::parse(storage.bytes(data), u"p"_s, "QCPL", 10);
        QVERIFY(payload);
        const QByteArray older = payload->bytes.chopped(static_cast<qsizetype>(sizeof(qint32)));
        const QcPlayerStorage::Result old = storage.parse(u"Raw"_s, QcSaveFile::bytes("QCPL", older, 9));
        QVERIFY(old && old->player);
        QCOMPARE(old->player->air, QcAirSupply::maxAir);
    }

    void airSurvivesRejoining()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QcServerConfig config;
        config.worldDirectory = directory.path();
        join(config);
        waterColumn(m_feet);
        ticks(50);
        QCOMPARE(player().air.air(), 250);
        QVERIFY(m_server->save().has_value());
        m_client.reset();
        m_server.reset();

        // The player comes back with the saved air, still in the water: join() ticks twice.
        join(config);
        QCOMPARE(player().air.air(), 248);
        QVERIFY(!m_client->airMessages.isEmpty());
        QCOMPARE(m_client->airMessages.constFirst().air, 249);
    }

    void landCreaturesDrownSwimmersDoNot()
    {
        join();
        const QcBlockPos pigCell = m_feet.offset(2, 0, -2);
        const QcBlockPos drownedCell = m_feet.offset(-2, 0, -2);
        const QcBlockPos codCell = m_feet.offset(-2, 0, 2);
        waterColumn(pigCell);
        waterColumn(drownedCell);
        waterColumn(codCell);
        const quint32 pig = creatureAt(u"qtcraft:pig"_s, pigCell);
        const quint32 drowned = creatureAt(u"qtcraft:drowned"_s, drownedCell);
        const quint32 cod = creatureAt(u"qtcraft:cod"_s, codCell);
        const QcBlockPos zombieCell = m_feet.offset(2, 0, 2);
        waterColumn(zombieCell);
        const quint32 zombie = creatureAt(u"qtcraft:zombie"_s, zombieCell);
        const double zombieHealth = m_server->mobs().find(zombie)->health;
        const double drownedHealth = m_server->mobs().find(drowned)->health;
        const double codHealth = m_server->mobs().find(cod)->health;

        QVERIFY(!m_content->mobs()[*m_content->mobIndex(u"qtcraft:pig"_s)].breathesUnderWater);
        QVERIFY(m_content->mobs()[*m_content->mobIndex(u"qtcraft:drowned"_s)].breathesUnderWater);
        QVERIFY(m_content->mobs()[*m_content->mobIndex(u"qtcraft:cod"_s)].breathesUnderWater);
        for (const QString &breather : {u"qtcraft:iron_golem"_s, u"qtcraft:sand_golem"_s, u"qtcraft:zombie"_s, u"qtcraft:husk"_s, u"qtcraft:skeleton"_s,
                                        u"qtcraft:stray"_s, u"qtcraft:mannequin"_s, u"qtcraft:caped_mannequin"_s}) {
            QVERIFY2(m_content->mobs()[*m_content->mobIndex(breather)].breathesUnderWater, qPrintable(breather));
        }

        ticks(320);
        QCOMPARE(m_server->mobs().find(pig)->air.air(), 0);
        QCOMPARE(m_server->mobs().find(pig)->health, 8.0);
        QCOMPARE(m_server->mobs().find(drowned)->health, drownedHealth);
        QCOMPARE(m_server->mobs().find(drowned)->air.air(), QcAirSupply::maxAir);
        QCOMPARE(m_server->mobs().find(cod)->health, codHealth);
        // Undead do not breathe, as in the reference.
        QCOMPARE(m_server->mobs().find(zombie)->health, zombieHealth);
        QCOMPARE(m_server->mobs().find(zombie)->air.air(), QcAirSupply::maxAir);

        // The pig drowns in the end and leaves its loot.
        for (int i = 0; i < 200 && m_server->mobs().find(pig); ++i) {
            ticks(1);
        }
        QVERIFY(!m_server->mobs().find(pig));
        QVERIFY(m_server->mobs().find(drowned));
        QCOMPARE(droppedCount(item("qtcraft:raw_porkchop")), 2);
    }

    void smallCreatureSwimmingAtTheSurfaceBreathes()
    {
        // An open pool two blocks deep: a silverfish with its mind swims up and keeps its eyes
        // above the surface.
        join();
        const QcBlockPos cell = m_feet.offset(3, 0, 3);
        waterColumn(cell);
        m_server->world().setBlock(cell.offset(0, 2, 0), qcAirBlockId);
        const quint32 silverfish = creatureAt(u"qtcraft:silverfish"_s, cell);
        m_server->mobs().find(silverfish)->noAi = false;
        const double health = m_server->mobs().find(silverfish)->health;
        ticks(400);
        QVERIFY(m_server->mobs().find(silverfish));
        QCOMPARE(m_server->mobs().find(silverfish)->health, health);
    }
};

QTEST_GUILESS_MAIN(TestDrowning)
#include "test_drowning.moc"
