// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/controllers/QcGeneratorUpgradeController.h"

#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>

// The width of the transition the question offers (decision E-1040): 6 chunks until changed, and a
// width out of range is refused with a warning.
class TestGeneratorUpgradeController : public QObject
{
    Q_OBJECT

private slots:
    void theWidthStaysInItsRange()
    {
        QcGeneratorUpgradeController controller({});
        QCOMPARE(controller.blendChunks(), 6);
        QCOMPARE(QcGeneratorUpgradeController::minBlendChunks(), 2);
        QCOMPARE(QcGeneratorUpgradeController::maxBlendChunks(), 16);
        QSignalSpy changed(&controller, &QcGeneratorUpgradeController::blendChunksChanged);

        controller.setBlendChunks(16);
        QCOMPARE(controller.blendChunks(), 16);
        QCOMPARE(changed.size(), 1);
        // The same width again is no change, and says nothing.
        controller.setBlendChunks(16);
        QCOMPARE(changed.size(), 1);

        for (const int refused : {1, 17, -3}) {
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("a transition of %1 chunks refused").arg(refused)));
            controller.setBlendChunks(refused);
            QCOMPARE(controller.blendChunks(), 16);
        }
        QCOMPARE(changed.size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestGeneratorUpgradeController)
#include "test_generator_upgrade_controller.moc"
