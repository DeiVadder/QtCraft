// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/meshing/QcBlockRenderTable.h"
#include "clientcore/session/QcClientEntities.h"
#include "clientcore/textures/QcTextureAtlas.h"
#include "QcPredictionFixture.h"
#include "QcSessionMeshes.h"

#include <QHash>
#include <QTest>

#include <array>

using namespace Qt::StringLiterals;

// The client predicts its player's breaks and placements against a local server (decision E-853):
// a creative break shows before the server answered and its confirmation changes nothing; survival
// predicts only at the end of the mining; refusals (out of reach, adventure mode, mining too fast,
// the item gone, another player first) roll back to the server's block; a rollback brings the old mesh
// back and a confirmation meshes nothing again; the numbers start anew after a reconnect. A held use
// button places again every four ticks (decision E-885).
namespace {

// Cells a session's prediction or rollback set (playerBlockChanged), in order.
struct OwnChanges
{
    QList<QcBlockPos> cells;
    // Cells the server set (blockChanged).
    QList<QcBlockPos> server;
};

} // namespace

class TestBlockPrediction : public QObject
{
    Q_OBJECT

private:
    using TickMode = QcClientSession::TickMode;

    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    std::unique_ptr<QcPredictionFixture> m_fixture;
    QcBlockId m_stone = m_content->blockId(u"qtcraft:stone"_s).value_or(qcAirBlockId);
    QcBlockId m_dirt = m_content->blockId(u"qtcraft:dirt"_s).value_or(qcAirBlockId);

    QcPredictionFixture &fixture() { return *m_fixture; }

    std::unique_ptr<QcClientSession> start(QcGameMode mode, const QString &name = u"Ann"_s)
    {
        if (!fixture().startHost(mode, QString::fromLatin1(QTest::currentTestFunction()))) {
            return nullptr;
        }
        auto session = fixture().join(name, TickMode::Manual);
        if (!session || !fixture().flattenAround(*session)) {
            QcTestUtil::failCase(u"no flat ground"_s);
        }
        return session;
    }

    static void record(QcClientSession &session, OwnChanges &changes)
    {
        QObject::connect(&session, &QcClientSession::playerBlockChanged, &session, [&changes](QcBlockPos pos, QcBlockId) { changes.cells.append(pos); });
        QObject::connect(&session, &QcClientSession::blockChanged, &session, [&changes](QcBlockPos pos, QcBlockId) { changes.server.append(pos); });
    }

    // A stone block two cells in front of the player's feet, at the height of the ground, aimed at
    // from above, with air above it.
    QcBlockPos stoneInFront(QcClientSession &session)
    {
        const QcBlockPos target = QcPredictionFixture::feet(session).offset(2, -1, 0);
        if (!fixture().clearCellOnStone(session, target.offset(0, 1, 0)) || !QcPredictionFixture::aim(session, target, QcBlockFace::Up)) {
            QcTestUtil::failCase(u"no stone to aim at"_s);
        }
        return target;
    }

    // Turns the player towards `block` until the hand targets it (any face: a lever's small box).
    static bool aimAnyFace(QcClientSession &session, QcBlockPos block)
    {
        for (const QcBlockFace face : {QcBlockFace::Up, QcBlockFace::West, QcBlockFace::East, QcBlockFace::North, QcBlockFace::South}) {
            QcControlAim::lookAt(session, block, face);
            const std::optional<QcRaycastHit> &target = session.hand().target();
            if (target && target->block == block) {
                return true;
            }
        }
        return false;
    }

    // The messages a second hand on the session's view (its prediction and inventory) sends while the
    // use button is held for one repeat period (QcHandActions::tickUse), aimed like the session's.
    QList<QcMessage> heldUseSends(QcClientSession &session, bool mining)
    {
        const QcClientEntities entities;
        QList<QcMessage> sent;
        quint32 predicted = 0;
        QcHandActions hand({session.prediction(), session.inventoryMirror(), entities}, session.gameMode(),
                           [&sent](const QcMessage &message) { sent.append(message); }, [](const QcSoundCue &) {},
                           [&predicted](const QList<QcMsg::BlockUpdate> &) { return ++predicted; });
        const QcContentRegistry &content = *session.worldMirror().content();
        hand.refreshTarget(*session.world(), content);
        hand.setMining(mining);
        hand.holdUse();
        for (int tick = 0; tick < QcHandActions::useRepeatTicks; ++tick) {
            std::ignore = hand.tickUse(*session.world(), content);
        }
        return sent;
    }

    // A break that the server refuses for a reason it knows before the client does: the block shows
    // gone at once and comes back with the acknowledgement.
    void checkRefusedBreak(const QString &serverCommand)
    {
        auto session = start(QcGameMode::Creative);
        QVERIFY(session);
        QVERIFY(fixture().waitFor([&] { return session->gameMode().rules().instantBreak; }));
        const QcBlockPos target = stoneInFront(*session);
        OwnChanges changes;
        record(*session, changes);

        fixture().host().runConsoleCommand(serverCommand);
        session->setMining(true);
        session->setMining(false);
        QCOMPARE(session->world()->blockAt(target), qcAirBlockId);
        QVERIFY(fixture().waitFor([&] { return QcPredictionFixture::settled(*session); }));
        QCOMPARE(session->world()->blockAt(target), m_stone);
        QCOMPARE(changes.cells, (QList<QcBlockPos>{target, target}));
        fixture().forget(session.get());
    }

private slots:
    void init() { m_fixture = std::make_unique<QcPredictionFixture>(m_content); }
    void cleanup() { m_fixture.reset(); }

    void aCreativeBreakShowsAtOnceAndIsConfirmed()
    {
        auto session = start(QcGameMode::Creative);
        QVERIFY(session);
        QVERIFY(fixture().waitFor([&] { return session->gameMode().rules().instantBreak; }));
        const QcBlockPos target = stoneInFront(*session);
        OwnChanges changes;
        record(*session, changes);

        // The press breaks at once (no tick ran), before any answer of the server was processed.
        session->setMining(true);
        session->setMining(false);
        QCOMPARE(session->world()->blockAt(target), qcAirBlockId);
        QCOMPARE(session->blockSequence(), 1u);
        QVERIFY(!QcPredictionFixture::settled(*session));
        QCOMPARE(changes.cells, QList<QcBlockPos>{target});

        // The server's answer equals the prediction: nothing changes again.
        QVERIFY(fixture().waitFor([&] { return QcPredictionFixture::settled(*session); }));
        QCOMPARE(session->world()->blockAt(target), qcAirBlockId);
        QCOMPARE(changes.cells, QList<QcBlockPos>{target});
        QVERIFY(!changes.server.contains(target));
        fixture().forget(session.get());
    }

    void withoutPredictionTheServerSetsTheBlock()
    {
        // QTCRAFT_BLOCK_PREDICTION=0: the block goes with the server's answer at the next tick.
        auto session = start(QcGameMode::Creative);
        QVERIFY(session);
        session->setBlockPrediction(false);
        QVERIFY(fixture().waitFor([&] { return session->gameMode().rules().instantBreak; }));
        const QcBlockPos target = stoneInFront(*session);
        OwnChanges changes;
        record(*session, changes);

        session->setMining(true);
        QCOMPARE(session->world()->blockAt(target), m_stone);
        QVERIFY(fixture().waitFor([&] { return session->world()->blockAt(target) == qcAirBlockId; }));
        session->setMining(false);
        QVERIFY(changes.cells.isEmpty());
        QVERIFY(changes.server.contains(target));
        QCOMPARE(session->blockSequence(), 0u);
        fixture().forget(session.get());
    }

    void aBreakOutOfTheServersReachRollsBack()
    {
        // The server moved the player away; the client does not know it yet.
        checkRefusedBreak(u"tp Ann 8 120 8"_s);
    }

    void aBreakInAdventureModeRollsBack()
    {
        // The server made the player an adventurer, who may break nothing without allow-lists.
        checkRefusedBreak(u"gamemode adventure Ann"_s);
    }

    void survivalPredictsOnlyAtTheEndOfMining()
    {
        auto session = start(QcGameMode::Survival);
        QVERIFY(session);
        const QcBlockPos target = stoneInFront(*session);
        QVERIFY(fixture().setBlock(*session, target, u"qtcraft:dirt"_s));
        QVERIFY(QcPredictionFixture::aim(*session, target, QcBlockFace::Up));
        OwnChanges changes;
        record(*session, changes);

        // Ticks at the server's pace (it times the mining in its players' time).
        session->setMining(true);
        bool broken = false;
        for (int tick = 0; tick < 100 && !broken; ++tick) {
            QCOMPARE(session->world()->blockAt(target), m_dirt);
            QVERIFY(changes.cells.isEmpty());
            session->tick();
            broken = session->world()->blockAt(target) == qcAirBlockId;
            if (!broken) {
                QTest::qWait(50);
            }
        }
        session->setMining(false);
        QVERIFY(broken);
        // Gone at the tick that ended the mining, before the server answered.
        QVERIFY(!QcPredictionFixture::settled(*session));
        QCOMPARE(changes.cells, QList<QcBlockPos>{target});
        QVERIFY(fixture().waitFor([&] { return QcPredictionFixture::settled(*session); }));
        QCOMPARE(session->world()->blockAt(target), qcAirBlockId);
        QCOMPARE(changes.cells, QList<QcBlockPos>{target});
        fixture().forget(session.get());
    }

    void survivalMiningTooFastRollsBack()
    {
        auto session = start(QcGameMode::Survival);
        QVERIFY(session);
        const QcBlockPos target = stoneInFront(*session);
        QVERIFY(fixture().setBlock(*session, target, u"qtcraft:dirt"_s));
        QVERIFY(QcPredictionFixture::aim(*session, target, QcBlockFace::Up));
        OwnChanges changes;
        record(*session, changes);

        // All ticks of the mining at once: the server has not seen the time pass and refuses.
        session->setMining(true);
        for (int tick = 0; tick < 100 && session->world()->blockAt(target) == m_dirt; ++tick) {
            session->tick();
        }
        session->setMining(false);
        QCOMPARE(session->world()->blockAt(target), qcAirBlockId);
        QVERIFY(fixture().waitFor([&] { return QcPredictionFixture::settled(*session); }));
        QCOMPARE(session->world()->blockAt(target), m_dirt);
        QCOMPARE(changes.cells, (QList<QcBlockPos>{target, target}));
        fixture().forget(session.get());
    }

    void aPlacementWithoutTheItemRollsBack()
    {
        // One dirt in the hand, two placements before the inventory came back: the second has no
        // item on the server.
        auto session = start(QcGameMode::Survival);
        QVERIFY(session);
        QVERIFY(fixture().give(*session, u"qtcraft:dirt"_s, 1));
        const QcBlockPos feet = QcPredictionFixture::feet(*session);
        const QcBlockPos first = feet.offset(2, 0, 0);
        const QcBlockPos second = feet.offset(-2, 0, 0);
        QVERIFY(fixture().clearCellOnStone(*session, first));
        QVERIFY(fixture().clearCellOnStone(*session, second));
        OwnChanges changes;
        record(*session, changes);

        QVERIFY(QcPredictionFixture::aim(*session, first.offset(0, -1, 0), QcBlockFace::Up));
        QVERIFY(session->useItem());
        QVERIFY(QcPredictionFixture::aim(*session, second.offset(0, -1, 0), QcBlockFace::Up));
        QVERIFY(session->useItem());
        QCOMPARE(session->world()->blockAt(first), m_dirt);
        QCOMPARE(session->world()->blockAt(second), m_dirt);
        QCOMPARE(session->blockSequence(), 2u);

        QVERIFY(fixture().waitFor([&] { return QcPredictionFixture::settled(*session); }));
        QCOMPARE(session->world()->blockAt(first), m_dirt);
        QCOMPARE(session->world()->blockAt(second), qcAirBlockId);
        QCOMPARE(changes.cells, (QList<QcBlockPos>{first, second, second}));
        fixture().forget(session.get());
    }

    void twoPlayersRacingForACellSeeTheWinnersBlock()
    {
        QVERIFY(fixture().startHost(QcGameMode::Creative, u"Race"_s));
        auto ann = fixture().join(u"Ann"_s, TickMode::Manual);
        auto bob = fixture().join(u"Bob"_s, TickMode::Manual);
        QVERIFY(ann && bob);
        QVERIFY(fixture().flattenAround(*ann));
        QVERIFY(fixture().give(*ann, u"qtcraft:stone"_s, 1));
        QVERIFY(fixture().give(*bob, u"qtcraft:dirt"_s, 1));
        const QcBlockPos cell = QcPredictionFixture::feet(*bob).offset(0, 0, 3);
        QVERIFY(fixture().clearCellOnStone(*ann, cell));
        QVERIFY(fixture().waitFor([&] { return bob->world()->blockAt(cell) == qcAirBlockId && bob->world()->blockAt(cell.offset(0, -1, 0)) == m_stone; }));
        QVERIFY(QcPredictionFixture::aim(*ann, cell.offset(0, -1, 0), QcBlockFace::Up));
        QVERIFY(QcPredictionFixture::aim(*bob, cell.offset(0, -1, 0), QcBlockFace::Up));

        // Bob's request reaches the server first; each sees his own block at once.
        QVERIFY(bob->useItem());
        QVERIFY(ann->useItem());
        QCOMPARE(bob->world()->blockAt(cell), m_dirt);
        QCOMPARE(ann->world()->blockAt(cell), m_stone);

        QVERIFY(fixture().waitFor([&] { return QcPredictionFixture::settled(*ann) && QcPredictionFixture::settled(*bob); }));
        QCOMPARE(bob->world()->blockAt(cell), m_dirt);
        QCOMPARE(ann->world()->blockAt(cell), m_dirt);
        fixture().forget(ann.get());
        fixture().forget(bob.get());
    }

    void aRollbackBringsTheOldMeshBackAndAConfirmationMeshesNothing()
    {
        auto session = start(QcGameMode::Creative);
        QVERIFY(session);
        QVERIFY(fixture().waitFor([&] { return session->gameMode().rules().instantBreak; }));
        const QcBlockPos feet = QcPredictionFixture::feet(*session);
        const QcBlockPos target = stoneInFront(*session);
        const auto table = std::make_shared<const QcBlockRenderTable>(*m_content, QcTextureAtlas::build(*m_content));
        QcSessionMeshes meshes(*session, table, 16);
        const quint64 key = QcSessionMeshes::sectionOf(target);
        QVERIFY(fixture().waitFor([&] { return meshes.deliveries(key) > 0 && meshes.idle(); }));
        const qsizetype before = meshes.vertices(key);

        // Refused (out of reach): the predicted mesh shows, then the old one again.
        fixture().host().runConsoleCommand(u"tp Ann 8 120 8"_s);
        session->setMining(true);
        session->setMining(false);
        meshes.deliverFinished();
        QVERIFY(meshes.vertices(key) != before);
        QVERIFY(fixture().waitFor([&] { return QcPredictionFixture::settled(*session) && meshes.idle(); }));
        QCOMPARE(meshes.vertices(key), before);

        // Confirmed: after the predicted mesh nothing is meshed again. The player falls back first.
        QVERIFY(fixture().waitFor([&] { return QcPredictionFixture::feet(*session) == feet && session->playerMotion().onGround; }));
        QVERIFY(QcPredictionFixture::aim(*session, target, QcBlockFace::Up));
        QVERIFY(fixture().waitFor([&] { return meshes.idle(); }));
        session->setMining(true);
        session->setMining(false);
        meshes.deliverFinished();
        const int predicted = meshes.deliveries(key);
        QVERIFY(meshes.vertices(key) != before);
        QVERIFY(fixture().waitFor([&] { return QcPredictionFixture::settled(*session) && meshes.idle(); }));
        QCOMPARE(meshes.deliveries(key), predicted);
        QCOMPARE(session->world()->blockAt(target), qcAirBlockId);
        fixture().forget(session.get());
    }

    void aHeldUseButtonPlacesEveryFourTicksUntilReleased()
    {
        // Decision E-885: the press places at once, the held button again after useRepeatTicks
        // ticks (against the block just placed, which the aim now hits), never in between, and
        // nothing more once released.
        auto session = start(QcGameMode::Survival);
        QVERIFY(session);
        QVERIFY(fixture().give(*session, u"qtcraft:dirt"_s, 8));
        const QcBlockPos cell = QcPredictionFixture::feet(*session).offset(2, 0, 0);
        QVERIFY(fixture().clearCellOnStone(*session, cell));
        QVERIFY(QcPredictionFixture::aim(*session, cell.offset(0, -1, 0), QcBlockFace::Up));
        OwnChanges changes;
        record(*session, changes);

        const quint64 pressedAt = session->tickCount();
        QVERIFY(session->pressUse());
        QCOMPARE(session->blockSequence(), 1u);
        constexpr quint64 repeat = QcHandActions::useRepeatTicks;
        QVERIFY(fixture().waitFor([&] { return session->tickCount() >= pressedAt + repeat - 1; }));
        QCOMPARE(session->blockSequence(), 1u);
        QVERIFY(fixture().waitFor([&] { return session->tickCount() >= pressedAt + repeat; }));
        QCOMPARE(session->blockSequence(), 2u);
        QCOMPARE(session->world()->blockAt(cell), m_dirt);

        session->releaseUse();
        const quint64 releasedAt = session->tickCount();
        QVERIFY(fixture().waitFor([&] { return session->tickCount() >= releasedAt + 3 * repeat; }));
        QCOMPARE(session->blockSequence(), 2u);
        QVERIFY(fixture().waitFor([&] { return QcPredictionFixture::settled(*session); }));
        // Both placements stand where they were predicted: the server took both.
        QCOMPARE(changes.cells.size(), 2);
        QCOMPARE(changes.cells.first(), cell);
        QVERIFY(changes.cells.last() != cell);
        QCOMPARE(session->world()->blockAt(cell), m_dirt);
        QCOMPARE(session->world()->blockAt(changes.cells.last()), m_dirt);
        fixture().forget(session.get());
    }

    void aSingleUseAndACancelledHoldDoNotRepeat()
    {
        // useItem (scripts, control commands) is one use; cancelUse (a screen opened) ends a hold.
        auto session = start(QcGameMode::Survival);
        QVERIFY(session);
        QVERIFY(fixture().give(*session, u"qtcraft:dirt"_s, 8));
        const QcBlockPos cell = QcPredictionFixture::feet(*session).offset(2, 0, 0);
        QVERIFY(fixture().clearCellOnStone(*session, cell));
        QVERIFY(QcPredictionFixture::aim(*session, cell.offset(0, -1, 0), QcBlockFace::Up));

        QVERIFY(session->useItem());
        quint64 since = session->tickCount();
        constexpr quint64 repeat = QcHandActions::useRepeatTicks;
        QVERIFY(fixture().waitFor([&] { return session->tickCount() >= since + 2 * repeat; }));
        QCOMPARE(session->blockSequence(), 1u);

        QVERIFY(session->pressUse());
        QCOMPARE(session->blockSequence(), 2u);
        QVERIFY(session->hand().useHeld());
        session->cancelUse();
        QVERIFY(!session->hand().useHeld());
        since = session->tickCount();
        QVERIFY(fixture().waitFor([&] { return session->tickCount() >= since + 2 * repeat; }));
        QCOMPARE(session->blockSequence(), 2u);
        fixture().forget(session.get());
    }

    void aHeldUseRepeatsOnlyForecastPlacements()
    {
        // Decision E-885: the held button repeats only where a placed block is forecast. Reacting
        // blocks (a door, a crafting table), food (planting food too: the server eats it where it cannot be
        // planted, decision E-292) and buckets send nothing; nor does anything while mining.
        auto session = start(QcGameMode::Survival);
        QVERIFY(session);
        const QcBlockPos cell = QcPredictionFixture::feet(*session).offset(2, 0, 0);
        const QcBlockPos ground = cell.offset(0, -1, 0);
        QVERIFY(fixture().clearCellOnStone(*session, cell));

        struct Case
        {
            const char *ground;
            const char *item;
            qsizetype sends;
        };
        static constexpr std::array cases{
            Case{"qtcraft:stone", "qtcraft:dirt", 1},     Case{"qtcraft:farmland", "qtcraft:carrot", 0},
            Case{"qtcraft:farmland", "qtcraft:potato", 0}, Case{"qtcraft:stone", "qtcraft:bread", 0},
            Case{"qtcraft:stone", "qtcraft:bucket", 0},   Case{"qtcraft:stone", "qtcraft:water_bucket", 0},
            Case{"qtcraft:crafting_table", "qtcraft:dirt", 0},    Case{"qtcraft:oak_door", "qtcraft:dirt", 0},
        };
        // Each item in a hotbar slot of its own (a second give of an item would stack onto the first).
        QHash<QString, int> slotOf;
        for (const Case &held : cases) {
            const QString item = QString::fromLatin1(held.item);
            if (slotOf.contains(item)) {
                session->selectHotbarSlot(slotOf.value(item));
                QVERIFY(fixture().waitFor([&] { return session->inventory().selectedStack().item == m_content->itemId(item); }));
            } else {
                const int slot = static_cast<int>(slotOf.size());
                slotOf.insert(item, slot);
                session->selectHotbarSlot(slot);
                QVERIFY(fixture().give(*session, item, 1));
            }
            QVERIFY(fixture().setBlock(*session, ground, QString::fromLatin1(held.ground)));
            QVERIFY2(aimAnyFace(*session, ground), held.ground);
            QVERIFY2(heldUseSends(*session, false).size() == held.sends, qPrintable(item + u" on "_s + QString::fromLatin1(held.ground)));
        }
        QCOMPARE(slotOf.size(), 6);

        // Dirt on stone again, but the mining button is held too: nothing repeats.
        QVERIFY(fixture().setBlock(*session, ground, u"qtcraft:stone"_s));
        session->selectHotbarSlot(0);
        QVERIFY(fixture().waitFor([&] { return session->inventory().selectedStack().item == m_content->itemId(u"qtcraft:dirt"_s); }));
        QVERIFY(QcPredictionFixture::aim(*session, ground, QcBlockFace::Up));
        QCOMPARE(heldUseSends(*session, false).size(), 1);
        QVERIFY(heldUseSends(*session, true).isEmpty());
        fixture().forget(session.get());
    }

    void numbersStartAnewAfterAReconnect()
    {
        auto session = start(QcGameMode::Creative);
        QVERIFY(session);
        QVERIFY(fixture().waitFor([&] { return session->gameMode().rules().instantBreak; }));
        const QcBlockPos target = stoneInFront(*session);
        session->setMining(true);
        session->setMining(false);
        QCOMPARE(session->blockSequence(), 1u);
        QVERIFY(fixture().waitFor([&] { return QcPredictionFixture::settled(*session); }));
        session->disconnectFromServer();
        QVERIFY(fixture().waitFor([&] { return session->state() == QcClientSession::State::Disconnected; }));
        fixture().forget(session.get());
        session.reset();

        // The same player again: the new session counts from 1, and the server answers it.
        auto again = fixture().join(u"Ann"_s, TickMode::Manual);
        QVERIFY(again);
        QVERIFY(fixture().waitFor([&] { return again->gameMode().rules().instantBreak; }));
        QCOMPARE(again->world()->blockAt(target), qcAirBlockId);
        const QcBlockPos next = stoneInFront(*again);
        again->setMining(true);
        again->setMining(false);
        QCOMPARE(again->blockSequence(), 1u);
        QCOMPARE(again->world()->blockAt(next), qcAirBlockId);
        QVERIFY(fixture().waitFor([&] { return QcPredictionFixture::settled(*again); }));
        QCOMPARE(again->world()->blockAt(next), qcAirBlockId);
        fixture().forget(again.get());
    }
};

QTEST_MAIN(TestBlockPrediction)
#include "test_block_prediction.moc"
