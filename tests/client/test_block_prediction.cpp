#include "clientcore/meshing/QcBlockRenderTable.h"
#include "clientcore/textures/QcTextureAtlas.h"
#include "QcPredictionFixture.h"
#include "QcSessionMeshes.h"

#include <QTest>

using namespace Qt::StringLiterals;

// The client predicts its player's breaks and placements against a local server (decision E-853):
// a creative break shows before the server answered and its confirmation changes nothing; survival
// predicts only at the end of the mining; refusals (out of reach, adventure mode, mining too fast,
// the item gone, another player first) roll back to the server's block; a rollback brings the old mesh
// back and a confirmation meshes nothing again; the numbers start anew after a reconnect.
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
