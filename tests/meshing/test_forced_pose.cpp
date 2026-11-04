// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

// The test hook `--pose draw:<0..1>` (decision E-984): the frozen draw that the pictures of a bow
// rely on.

#include "app/rendering/QcForcedPose.h"

#include <QTest>

using namespace Qt::StringLiterals;

class TestForcedPose : public QObject
{
    Q_OBJECT

private slots:
    void drawReadsItsPower()
    {
        QcForcedPose forced;
        QVERIFY(!forced.draw());

        forced.set(u"draw:0.5"_s);
        QCOMPARE(forced.draw(), std::optional(0.5));
        forced.set(u"draw:1"_s);
        QCOMPARE(forced.draw(), std::optional(1.0));
        forced.set(u"draw:0"_s);
        QCOMPARE(forced.draw(), std::optional(0.0));
    }

    void drawStaysWithinAFullDraw()
    {
        QcForcedPose forced;
        forced.set(u"draw:3"_s);
        QCOMPARE(forced.draw(), std::optional(1.0));
        forced.set(u"draw:-2"_s);
        QCOMPARE(forced.draw(), std::optional(0.0));
    }

    void otherTextIsNoDraw()
    {
        QcForcedPose forced;
        for (const QString &pose : {u"draw"_s, u"draw:"_s, u"draw:much"_s, u"swing:0.5"_s, u"hurt:0.5"_s}) {
            forced.set(pose);
            QVERIFY2(!forced.draw(), qPrintable(pose));
        }
    }

    // The frozen power reaches the pose of the local player; other poses leave the draw alone.
    void theLocalPoseCarriesTheDraw()
    {
        QcForcedPose forced;
        forced.set(u"draw:0.25"_s);
        QCOMPARE(forced.local({}).drawProgress, 0.25);

        forced.set(u"sneak"_s);
        QCOMPARE(forced.local({}).drawProgress, -1.0);
    }
};

QTEST_GUILESS_MAIN(TestForcedPose)
#include "test_forced_pose.moc"
