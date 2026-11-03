// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/camera/QcEyeHeight.h"
#include "common/physics/QcPhysics.h"

#include <QTest>

// The camera's eye height (decision E-961): it starts where it is told, drops to the crouching
// height over QcEyeHeight::transitionSeconds at a steady speed, whatever the frame rate, and rises
// back the same way.
class TestEyeHeight : public QObject
{
    Q_OBJECT

private slots:
    void theFirstFramePlacesTheEyes()
    {
        QcEyeHeight eyes;
        QCOMPARE(eyes.advance(QcPhysics::sneakEyeHeight, 0.016), QcPhysics::sneakEyeHeight);
        QCOMPARE(eyes.height(), QcPhysics::sneakEyeHeight);
    }

    void crouchingLowersTheEyesWithinATenthOfASecond()
    {
        QcEyeHeight eyes;
        eyes.advance(QcPhysics::playerEyeHeight, 0.0);

        // Half the transition: half the way down.
        const double half = eyes.advance(QcPhysics::sneakEyeHeight, QcEyeHeight::transitionSeconds * 0.5);
        QVERIFY(qAbs(half - (QcPhysics::playerEyeHeight + QcPhysics::sneakEyeHeight) * 0.5) < 1e-9);
        // Many small frames reach the bottom and stay there.
        for (int frame = 0; frame < 10; ++frame) {
            eyes.advance(QcPhysics::sneakEyeHeight, 1.0 / 120.0);
        }
        QCOMPARE(eyes.height(), QcPhysics::sneakEyeHeight);

        // Standing up rises within the same time, in two frames.
        eyes.advance(QcPhysics::playerEyeHeight, QcEyeHeight::transitionSeconds * 0.6);
        QVERIFY(eyes.height() < QcPhysics::playerEyeHeight);
        QCOMPARE(eyes.advance(QcPhysics::playerEyeHeight, QcEyeHeight::transitionSeconds * 0.6), QcPhysics::playerEyeHeight);
    }

    void aNegativeTimeJumpsToTheTarget()
    {
        QcEyeHeight eyes;
        eyes.advance(QcPhysics::playerEyeHeight, 0.0);
        QCOMPARE(eyes.advance(QcPhysics::sneakEyeHeight, -1.0), QcPhysics::sneakEyeHeight);
    }
};

QTEST_GUILESS_MAIN(TestEyeHeight)
#include "test_eye_height.moc"
