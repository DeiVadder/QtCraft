// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/session/QcCampfireSmoke.h"
#include "clientcore/session/QcEffectClock.h"
#include "clientcore/session/QcEnchantingAura.h"
#include "clientcore/session/QcWorldMirror.h"
#include "common/physics/QcPhysics.h"

#include <QTest>

#include <cmath>

// The clock of the world's animations (decision E-962) and the campfire smoke and enchanting runes
// it drives: they move with the clock, whatever the time of day does (a world without its daylight
// cycle, like the showcase, froze them before), stand while the server stands and show the same
// at the same moment whatever the frame rate.
class TestWorldEffects : public QObject
{
    Q_OBJECT

    // The animation seconds every frame shows when a client ticks its world mirror at 20 Hz and
    // draws `fps` frames a second for `seconds` of real time, each with its fraction of a tick, as
    // the renderer does (QcWorldMirror::animationSeconds).
    static QList<double> framesShown(double seconds, int fps)
    {
        QcWorldMirror mirror;
        quint64 ticks = 0;
        QList<double> shown;
        for (int frame = 0; frame <= qRound(seconds * fps); ++frame) {
            const double now = static_cast<double>(frame) / fps;
            while (static_cast<double>(ticks + 1) * QcPhysics::tickDuration <= now + 1e-9) {
                mirror.tick(++ticks);
            }
            const double alpha = (now - static_cast<double>(ticks) * QcPhysics::tickDuration) / QcPhysics::tickDuration;
            shown.append(mirror.animationSeconds(alpha));
        }
        return shown;
    }

    static QcVec3 tableTarget() { return {0.5, 65.15, 0.5}; }

private slots:
    void clockRunsWithTheServer()
    {
        QcEffectClock clock;
        for (int tick = 0; tick < 20; ++tick) {
            clock.tick(1.0);
        }
        QCOMPARE(clock.time(), 1.0);
        // A frame shows the time between the previous and the latest tick.
        QCOMPARE(clock.seconds(1.0), 1.0);
        QCOMPARE(clock.seconds(0.5), 1.0 - QcPhysics::tickDuration / 2.0);

        // Paused or frozen: it stands, also between ticks (no jitter with the frame's fraction),
        // and the first frame after the pause shows where the last one before it was heading.
        clock.tick(0.0);
        QCOMPARE(clock.seconds(0.0), 1.0);
        QCOMPARE(clock.seconds(0.9), 1.0);

        // A slower tick rate runs it slower, a sprint not faster than real time.
        clock.tick(0.5);
        QCOMPARE(clock.time(), 1.0 + QcPhysics::tickDuration / 2.0);
        clock.tick(5000.0);
        QCOMPARE(clock.time(), 1.0 + 1.5 * QcPhysics::tickDuration);
        clock.tick(-1.0);
        QCOMPARE(clock.time(), 1.0 + 1.5 * QcPhysics::tickDuration);
    }

    void shownTimeDoesNotDependOnTheFrameRate()
    {
        // Driven through the mirror's ticks and the frames' fractions at several frame rates: the
        // shown time never runs back, never jumps by more than a frame, and at the moments that
        // are frames of every rate (each half second) all rates show the same, one tick behind.
        const QList<int> rates{30, 60, 120, 144};
        QList<QList<double>> runs;
        for (const int fps : rates) {
            const QList<double> shown = framesShown(3.0, fps);
            for (qsizetype frame = 1; frame < shown.size(); ++frame) {
                const double step = shown.at(frame) - shown.at(frame - 1);
                QVERIFY2(step >= 0.0 && step <= 1.0 / fps + 1e-9, qPrintable(QString::number(step)));
            }
            runs.append(shown);
        }
        for (int half = 1; half <= 6; ++half) {
            const double moment = half * 0.5;
            for (qsizetype run = 0; run < rates.size(); ++run) {
                const double shown = runs.at(run).at(qRound(moment * rates.at(run)));
                QVERIFY2(std::abs(shown - (moment - QcPhysics::tickDuration)) < 1e-9, qPrintable(QString::number(shown)));
            }
        }
    }

    void animationsRunWithoutTheDaylightCycle()
    {
        // The showcase's rule: the time of day stands, the server runs at its normal rate.
        QcWorldMirror mirror;
        QVERIFY(mirror.handle(QcMsg::WorldTime{6000.0, 1200.0, false, 1.0}, {}, 0));
        const double before = mirror.animationSeconds(0.0);
        for (quint64 tick = 1; tick <= 20; ++tick) {
            mirror.tick(tick);
        }
        QCOMPARE(mirror.worldTime(), 6000.0);
        QCOMPARE(mirror.animationSeconds(1.0) - before, 1.0);

        // The smoke of a campfire rises between these moments.
        const QList<QcEffectPuff> early = QcCampfireSmoke::puffsOf({0, 64, 0}, before + 0.1);
        const QList<QcEffectPuff> late = QcCampfireSmoke::puffsOf({0, 64, 0}, before + 0.3);
        QCOMPARE(early.size(), late.size());
        QVERIFY(late.first().position.y != early.first().position.y);

        // Paused (speed 0): the frames between the message and the next tick go on as before, then
        // the animations stand where they were heading, between ticks too; nothing jumps back.
        const double lastFrame = mirror.animationSeconds(0.9);
        QVERIFY(mirror.handle(QcMsg::WorldTime{6000.0, 1200.0, false, 0.0}, {}, 21));
        QCOMPARE(mirror.animationSeconds(0.9), lastFrame);
        const double heading = mirror.animationSeconds(1.0);
        for (quint64 tick = 21; tick <= 40; ++tick) {
            mirror.tick(tick);
            QCOMPARE(mirror.animationSeconds(0.0), heading);
            QCOMPARE(mirror.animationSeconds(0.9), heading);
        }

        // Resumed: it goes on from there without a jump.
        QVERIFY(mirror.handle(QcMsg::WorldTime{6000.0, 1200.0, false, 1.0}, {}, 41));
        mirror.tick(41);
        QCOMPARE(mirror.animationSeconds(0.0), heading);
        QCOMPARE(mirror.animationSeconds(1.0), heading + QcPhysics::tickDuration);
    }

    void smokeRisesDriftsAndFades()
    {
        // A puff (index 0 of a loop starting at 0 s) over its lifetime of puffSeconds.
        const QcBlockPos fire{10, 70, -4};
        const double step = 0.25;
        double lastY = -1.0;
        double largest = 0.0;
        for (int index = 0; index * step < QcCampfireSmoke::puffSeconds - 1e-9; ++index) {
            const QcEffectPuff puff = QcCampfireSmoke::puffsOf(fire, index * step).constFirst();
            // Rises at 3.2 m per lifetime, about 1.07 m a second.
            if (lastY >= 0.0) {
                QCOMPARE_GT(puff.position.y, lastY);
                QVERIFY(std::abs((puff.position.y - lastY) / step - 3.2 / QcCampfireSmoke::puffSeconds) < 1e-6);
            }
            lastY = puff.position.y;
            // Drifts at most 0.4 blocks from the fire's middle.
            QVERIFY(std::abs(puff.position.x - (fire.x + 0.5)) <= 0.4);
            QVERIFY(std::abs(puff.position.z - (fire.z + 0.5)) <= 0.4);
            largest = std::max(largest, puff.size);
        }

        // It swells and shrinks again towards the end of its life, then starts anew at the fire.
        const QcEffectPuff end = QcCampfireSmoke::puffsOf(fire, QcCampfireSmoke::puffSeconds - 0.01).constFirst();
        const QcEffectPuff anew = QcCampfireSmoke::puffsOf(fire, QcCampfireSmoke::puffSeconds).constFirst();
        QVERIFY(largest > 0.35);
        QVERIFY(end.size < 0.2);
        QVERIFY(anew.position.y < fire.y + 0.5);
        QVERIFY(end.position.y > fire.y + 3.0);
    }

    void runesTravelFromTheShelfToTheTable()
    {
        QcEnchantingAura::Table table{{0, 64, 0}, {{0, 64, -2}}};
        QcEnchantingAura aura;

        // Follow the shelf's rune through one cycle: it appears, comes nearer and ends at the book.
        double lastDistance = -1.0;
        int flying = 0;
        for (int step = 0; step <= 64; ++step) {
            const double t = step * QcEnchantingAura::cycleSeconds / 64.0;
            const QList<QcEnchantingAura::Rune> &runes = aura.runesOf(table, t);
            if (runes.isEmpty()) {
                lastDistance = -1.0;
                continue;
            }
            const double distance = (runes.first().position - tableTarget()).length();
            if (lastDistance >= 0.0) {
                QCOMPARE_LT(distance, lastDistance + 0.05);
            }
            lastDistance = distance;
            ++flying;
        }
        // Flight takes flightSeconds of each cycle; it rests the rest of the cycle.
        QVERIFY(flying >= 30 && flying <= 34);

        // Shortly before the end of its flight it is at the book, at the start near the shelf.
        double start = -1.0;
        bool wasEmpty = aura.runesOf(table, 0.0).isEmpty();
        for (int centi = 1; start < 0.0 && centi < 200.0 * QcEnchantingAura::cycleSeconds; ++centi) {
            const double t = centi * 0.01;
            const bool empty = aura.runesOf(table, t).isEmpty();
            if (wasEmpty && !empty) {
                start = t;
            }
            wasEmpty = empty;
        }
        QVERIFY(start >= 0.0);
        const QcVec3 atStart = aura.runesOf(table, start + 0.01).first().position;
        QVERIFY((atStart - QcVec3{0.5, 64.8, -1.5}).length() < 1.0);
        const QcVec3 atEnd = aura.runesOf(table, start + QcEnchantingAura::flightSeconds - 0.02).first().position;
        QVERIFY((atEnd - tableTarget()).length() < 0.2);
        QVERIFY(aura.runesOf(table, start + QcEnchantingAura::flightSeconds + 0.02).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestWorldEffects)
#include "test_world_effects.moc"
