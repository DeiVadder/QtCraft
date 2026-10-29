// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/meshing/QcBlockRenderTable.h"
#include "clientcore/textures/QcTextureAtlas.h"
#include "QcPredictionFixture.h"
#include "QcSessionMeshes.h"

#include <QElapsedTimer>
#include <QTest>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

// Headless measurement of the block prediction (decision E-853), without a window: the time from the
// player's action (pressing the mining button in creative mode, using a block on a face) until the
// client has the new mesh of the changed section. A local server and a session that ticks on its own
// thread (as in the game); the meshes are pumped every 16 ms like frames. Interleaved with the
// prediction on and off (QcClientSession::setBlockPrediction, the state before stage 78), each idle
// and with a backlog of meshes (every loaded section dirty, as while chunks stream in).
// QTCRAFT_PREDICTION_MEASURE=full measures 20 actions per case, else 3. Prints "MEASURE key=value ..."
// lines. The latency itself is real time; nothing else waits for it.
namespace {

// What one measurement works with: the server, the player's session, its meshes and the cell
// broken and placed.
struct Bench
{
    QcPredictionFixture &fixture;
    QcClientSession &session;
    QcSessionMeshes &meshes;
    QcBlockPos target;
};

struct Case
{
    QString scenario;
    QString action;
    bool prediction = true;
    QList<double> ms;
};

double percentile(QList<double> values, double share)
{
    if (values.isEmpty()) {
        return 0.0;
    }
    std::ranges::sort(values);
    return values[static_cast<qsizetype>(std::lround(share * static_cast<double>(values.size() - 1)))];
}

} // namespace

class TestBlockPredictionMeasure : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    // Since the last placement: the server takes 10 uses a second (QcRateLimiter::Kind::Use, a burst
    // of 20); faster placements would be dropped and rolled back, as for a player.
    QElapsedTimer m_sinceUse;
    static constexpr qint64 usePaceMs = 110;

    // One action until the changed section's mesh arrived; the milliseconds, or -1 on a time out.
    double measure(const Bench &bench, const std::function<void()> &action)
    {
        QcClientSession &session = bench.session;
        QcSessionMeshes &meshes = bench.meshes;
        const QcBlockPos target = bench.target;
        const quint64 key = QcSessionMeshes::sectionOf(target);
        const int before = meshes.deliveries(key);
        qint64 changedAt = -1;
        const auto note = [&](QcBlockPos pos) {
            if (pos == target && changedAt < 0) {
                changedAt = meshes.clock().nsecsElapsed();
            }
        };
        const QMetaObject::Connection own = connect(&session, &QcClientSession::playerBlockChanged, this, [&](QcBlockPos pos, QcBlockId) { note(pos); });
        const QMetaObject::Connection server = connect(&session, &QcClientSession::blockChanged, this, [&](QcBlockPos pos, QcBlockId) { note(pos); });

        const qint64 startNs = meshes.clock().nsecsElapsed();
        action();
        // A mesh delivered after the change was made from it: older jobs are outdated by then.
        const bool done = bench.fixture.waitFor([&] {
            if (changedAt >= 0) {
                session.setMining(false);
            }
            return changedAt >= 0 && meshes.deliveries(key) > before && meshes.deliveredAtNs(key) >= changedAt;
        });
        disconnect(own);
        disconnect(server);
        session.setMining(false);
        return done ? static_cast<double>(meshes.deliveredAtNs(key) - startNs) / 1e6 : -1.0;
    }

    static void report(const Case &measured)
    {
        const auto ms = [](double value) { return QString::number(value, 'f', 2); };
        const double largest = measured.ms.isEmpty() ? 0.0 : std::ranges::max(measured.ms);
        qInfo().noquote() << u"MEASURE scenario=%1 action=%2 prediction=%3 count=%4 median_ms=%5 p90_ms=%6 max_ms=%7"_s.arg(
            measured.scenario, measured.action, measured.prediction ? u"on"_s : u"off"_s, QString::number(measured.ms.size()),
            ms(percentile(measured.ms, 0.5)), ms(percentile(measured.ms, 0.9)), ms(largest));
    }

    // One measured action of the case, after the session and the meshes settled.
    void step(const Bench &bench, Case &measured)
    {
        QcClientSession &session = bench.session;
        session.setBlockPrediction(measured.prediction);
        const bool breaking = measured.action == u"break"_s;
        QVERIFY(bench.fixture.waitFor([&] { return QcPredictionFixture::settled(session) && bench.meshes.idle(); }));
        QVERIFY(session.world()->blockAt(bench.target) == (breaking ? m_content->blockId(u"qtcraft:stone"_s) : qcAirBlockId));
        QVERIFY(QcPredictionFixture::aim(session, breaking ? bench.target : bench.target.offset(0, -1, 0), QcBlockFace::Up));
        if (!breaking && m_sinceUse.isValid() && m_sinceUse.elapsed() < usePaceMs) {
            QTest::qWait(static_cast<int>(usePaceMs - m_sinceUse.elapsed()));
        }
        if (measured.scenario == u"backlog"_s) {
            bench.meshes.scheduler().remeshAll();
        }
        const double ms = measure(bench, [&] {
            if (breaking) {
                session.setMining(true);
            } else {
                m_sinceUse.start();
                session.useItem();
            }
        });
        QVERIFY(ms >= 0.0);
        measured.ms.append(ms);
    }

private slots:
    void actionToMesh()
    {
        const int rounds = qEnvironmentVariable("QTCRAFT_PREDICTION_MEASURE") == u"full"_s ? 20 : 3;
        QcPredictionFixture fixture(m_content);
        QVERIFY(fixture.startHost(QcGameMode::Creative, u"Measure"_s));
        auto session = fixture.join(u"Ann"_s, QcClientSession::TickMode::Threaded);
        QVERIFY(session);
        QVERIFY(fixture.flattenAround(*session));
        QVERIFY(fixture.waitFor([&] { return session->gameMode().rules().instantBreak; }));
        QVERIFY(fixture.give(*session, u"qtcraft:stone"_s, 1));
        // A stone block on the floor beside the player: broken from above, placed again on the floor.
        const QcBlockPos target = QcPredictionFixture::feet(*session).offset(2, 0, 0);
        QVERIFY(fixture.clearCellOnStone(*session, target));
        QVERIFY(fixture.setBlock(*session, target, u"qtcraft:stone"_s));
        QcSessionMeshes meshes(*session, std::make_shared<const QcBlockRenderTable>(*m_content, QcTextureAtlas::build(*m_content)), 16);
        QVERIFY(fixture.waitFor([&] { return meshes.idle() && meshes.deliveries(QcSessionMeshes::sectionOf(target)) > 0; }));

        QList<Case> cases;
        for (const QString &scenario : {u"idle"_s, u"backlog"_s}) {
            for (const QString &action : {u"break"_s, u"place"_s}) {
                cases.append({scenario, action, true, {}});
                cases.append({scenario, action, false, {}});
            }
        }

        // A break and a placement take turns, so the cell is stone before each break and air before
        // each placement; prediction on and off take turns which goes first.
        const auto caseOf = [&cases](const QString &scenario, const QString &action, bool prediction) -> Case & {
            return *std::ranges::find_if(cases, [&](const Case &one) { return one.scenario == scenario && one.action == action && one.prediction == prediction; });
        };
        const Bench bench{fixture, *session, meshes, target};
        for (int round = 0; round < rounds && !QTest::currentTestFailed(); ++round) {
            for (const QString &scenario : {u"idle"_s, u"backlog"_s}) {
                for (const bool prediction : {round % 2 == 0, round % 2 != 0}) {
                    step(bench, caseOf(scenario, u"break"_s, prediction));
                    step(bench, caseOf(scenario, u"place"_s, prediction));
                }
            }
        }
        QVERIFY(!QTest::currentTestFailed());

        for (const Case &measured : std::as_const(cases)) {
            report(measured);
        }
#ifdef NDEBUG
        // The full measurement runs alone on the machine: in an optimised build every predicted
        // action reaches its mesh within a few milliseconds, also in the slow tenth (no wait for a
        // tick or the server).
        if (rounds >= 20) {
            for (const Case &measured : std::as_const(cases)) {
                QVERIFY2(!measured.prediction || percentile(measured.ms, 0.9) < 25.0,
                         qPrintable(u"%1 %2: p90 %3 ms"_s.arg(measured.scenario, measured.action).arg(percentile(measured.ms, 0.9))));
            }
        }
#endif
        fixture.forget(session.get());
    }
};

QTEST_MAIN(TestBlockPredictionMeasure)
#include "test_block_prediction_measure.moc"
