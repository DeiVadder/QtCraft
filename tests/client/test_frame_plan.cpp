#include "QcFramePlan.h"

#include <QTest>

// The frame of the world renderer as a list of steps (decision E-390): today's mode draws straight
// into the output in one GPU render pass; a mode with a shadow map and post-processing splits into
// three passes by target, without changing the steps it shares with today's mode.
class TestFramePlan : public QObject
{
    Q_OBJECT

private slots:
    void classicIsOneRenderPassIntoTheOutput()
    {
        const QcFramePlan plan = QcFramePlan::classic();
        QVERIFY(!plan.sectionLight);
        QVERIFY(plan.waterWithTranslucent());
        const QList<QcFramePlan::Group> groups = plan.groups();
        QCOMPARE(groups.size(), 1);
        QCOMPARE(groups[0].target, QcRenderTarget::Output);
        const QList<QcRenderPass> expected = {QcRenderPass::Sky, QcRenderPass::Opaque, QcRenderPass::Entities, QcRenderPass::Translucent,
                                              QcRenderPass::Weather, QcRenderPass::Overlay};
        QCOMPARE(groups[0].passes, expected);
    }

    void shadowAndPostProcessingGetTargetsOfTheirOwn()
    {
        QcFramePlan plan = QcFramePlan::classic();
        plan.passes.prepend(QcRenderPass::Shadow);
        plan.passes.insert(plan.passes.indexOf(QcRenderPass::Translucent), QcRenderPass::Water);
        plan.passes.append(QcRenderPass::PostProcess);
        QVERIFY(!plan.waterWithTranslucent());

        const QList<QcFramePlan::Group> groups = plan.groups();
        QCOMPARE(groups.size(), 3);
        QCOMPARE(groups[0].target, QcRenderTarget::ShadowMap);
        QCOMPARE(groups[0].passes, QList<QcRenderPass>{QcRenderPass::Shadow});
        QCOMPARE(groups[1].target, QcRenderTarget::Scene);
        QCOMPARE(groups[1].passes.size(), 7);
        QCOMPARE(groups[1].passes[4], QcRenderPass::Translucent);
        QCOMPARE(groups[2].target, QcRenderTarget::Output);
        QCOMPARE(groups[2].passes, QList<QcRenderPass>{QcRenderPass::PostProcess});

        // Without post-processing the scene is the output again.
        plan.passes.removeAll(QcRenderPass::PostProcess);
        QCOMPARE(plan.targetOf(QcRenderPass::Opaque), QcRenderTarget::Output);
        QCOMPARE(plan.groups().size(), 2);
    }

    // Every GPU render pass clears its target, so a plan must not return to a target: a shadow step
    // between two output steps would wipe the first output pass.
    void aPlanReturningToATargetIsInvalid()
    {
        QVERIFY(QcFramePlan::classic().visitsTargetsOnce());

        QcFramePlan plan = QcFramePlan::classic();
        plan.passes.prepend(QcRenderPass::Shadow);
        plan.passes.append(QcRenderPass::PostProcess);
        QVERIFY(plan.visitsTargetsOnce());

        QcFramePlan returning = QcFramePlan::classic();
        returning.passes.insert(1, QcRenderPass::Shadow);
        QVERIFY(!returning.visitsTargetsOnce());

        QcFramePlan sceneAgain = plan;
        sceneAgain.passes.append(QcRenderPass::Overlay);
        QVERIFY(!sceneAgain.visitsTargetsOnce());
    }
};

QTEST_GUILESS_MAIN(TestFramePlan)
#include "test_frame_plan.moc"
