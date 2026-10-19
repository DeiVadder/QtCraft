#include "clientcore/player/QcPrediction.h"
#include "clientcore/session/QcClientEntities.h"
#include "QcTestWorld.h"

#include <QTest>

// QcPrediction on its own: the reconciliation results, the cap of pending inputs and riding a cart.
class TestPrediction : public QObject
{
    Q_OBJECT

private:
    // A floor of stone at y = 0, the player standing on it at (0.5, 1, 0.5).
    static QcMsg::PlayerState standingState(quint32 acknowledged)
    {
        QcMsg::PlayerState state;
        state.lastInputSequence = acknowledged;
        state.motion.position = {0.5, 1.0, 0.5};
        state.motion.onGround = true;
        return state;
    }

    void layFloor(QcTestWorld &world) const
    {
        world.fillBox({-8, 0, -8}, {8, 0, 8}, *m_content.blockId(QStringLiteral("test:stone")));
    }

    QcContentRegistry m_content = qcMakeTestContent();

private slots:
    void theFirstStatePlacesAndLaterOnesApply()
    {
        QcTestWorld world(m_content);
        layFloor(world);
        QcPrediction prediction;
        QVERIFY(!prediction.hasServerState());

        QCOMPARE(prediction.reconcile(standingState(0), &world, false), QcPrediction::Reconciled::Placed);
        QVERIFY(prediction.hasServerState());
        // A placement is no correction, wherever the prediction stood before.
        QCOMPARE(prediction.lastCorrection(), 0.0);
        QCOMPARE(prediction.motion().position, (QcVec3{0.5, 1.0, 0.5}));

        QCOMPARE(prediction.reconcile(standingState(0), &world, false), QcPrediction::Reconciled::Applied);
    }

    void anOlderStateIsIgnored()
    {
        QcTestWorld world(m_content);
        layFloor(world);
        QcPrediction prediction;
        QCOMPARE(prediction.reconcile(standingState(5), &world, false), QcPrediction::Reconciled::Placed);

        QcMsg::PlayerState older = standingState(4);
        older.motion.position = {3.5, 1.0, 3.5};
        QCOMPARE(prediction.reconcile(older, &world, false), QcPrediction::Reconciled::Ignored);
        QCOMPARE(prediction.motion().position, (QcVec3{0.5, 1.0, 0.5}));
    }

    void acknowledgedInputsLeaveAndTheRestIsReplayed()
    {
        QcTestWorld world(m_content);
        layFloor(world);
        QcPrediction prediction;
        prediction.reconcile(standingState(0), &world, false);
        QcPlayerInput intent;
        intent.forward = 1.0f;
        prediction.setIntent(intent);

        for (int tick = 0; tick < 4; ++tick) {
            QCOMPARE(prediction.predict(world, false, false).sequence, quint32(tick + 1));
        }
        QCOMPARE(prediction.pendingInputCount(), 4);
        const QcVec3 predicted = prediction.motion().position;

        // The server acknowledged two inputs at the predicted start: replaying the other two moves
        // the player less far than the four did, which shows as a correction.
        QCOMPARE(prediction.reconcile(standingState(2), &world, false), QcPrediction::Reconciled::Applied);
        QCOMPARE(prediction.pendingInputCount(), 2);
        QVERIFY(prediction.lastCorrection() > 0.0);
        QCOMPARE(prediction.largestCorrection(), prediction.lastCorrection());
        QVERIFY(prediction.motion().position != predicted);
    }

    void pendingInputsStopAtTheCap()
    {
        QcTestWorld world(m_content);
        layFloor(world);
        QcPrediction prediction;
        prediction.reconcile(standingState(0), &world, false);

        for (qsizetype tick = 0; tick < QcPrediction::maxPendingInputs + 20; ++tick) {
            prediction.predict(world, false, false);
        }
        QCOMPARE(prediction.pendingInputCount(), QcPrediction::maxPendingInputs);

        // The oldest were dropped: acknowledging the first 20 leaves all that are kept.
        prediction.reconcile(standingState(20), &world, false);
        QCOMPARE(prediction.pendingInputCount(), QcPrediction::maxPendingInputs);
        prediction.reconcile(standingState(21), &world, false);
        QCOMPARE(prediction.pendingInputCount(), QcPrediction::maxPendingInputs - 1);
    }

    void aRiderIsNeitherPredictedNorCorrected()
    {
        QcTestWorld world(m_content);
        layFloor(world);
        QcClientEntities entities;
        QcPrediction prediction;
        prediction.reconcile(standingState(0), &world, false);

        // Getting in: the server names the cart; the state is no correction and is not replayed.
        QcMsg::PlayerState riding = standingState(0);
        riding.vehicle = 7;
        riding.motion.position = {4.5, 1.0, 4.5};
        QCOMPARE(prediction.reconcile(riding, &world, false), QcPrediction::Reconciled::Applied);
        QVERIFY(prediction.riding().isRiding());
        QCOMPARE(prediction.riding().vehicle(), quint32(7));
        QCOMPARE(prediction.lastCorrection(), 0.0);
        QCOMPARE(prediction.motion().position, (QcVec3{0.5, 1.0, 0.5}));

        // While the cart has not arrived in the mirror the player stays; the input is still sent.
        QcPlayerInput intent;
        intent.forward = 1.0f;
        prediction.setIntent(intent);
        prediction.predict(world, false, false);
        prediction.followCart(entities);
        QCOMPARE(prediction.motion().position, (QcVec3{0.5, 1.0, 0.5}));
        QCOMPARE(prediction.pendingInputCount(), 1);

        // Getting out places the player where the server says, again without a correction.
        QcMsg::PlayerState out = standingState(1);
        out.motion.position = {6.5, 1.0, 6.5};
        QCOMPARE(prediction.reconcile(out, &world, false), QcPrediction::Reconciled::Applied);
        QVERIFY(!prediction.riding().isRiding());
        QCOMPARE(prediction.lastCorrection(), 0.0);
        QCOMPARE(prediction.motion().position, (QcVec3{6.5, 1.0, 6.5}));
    }
};

QTEST_GUILESS_MAIN(TestPrediction)
#include "test_prediction.moc"
