#include "common/protocol/QcEntityMessages.h"
#include "QcTestUtil.h"
#include "server/mobs/QcMobBehaviour.h"
#include "server/mobs/QcMobManager.h"
#include "server/pathing/QcDoorPassage.h"
#include "server/pathing/QcMobLegs.h"

#include <QTest>

using namespace Qt::StringLiterals;

// The parts of the creature architecture of decision E-423: per-kind state as components, the pose
// with its box, eyes and protocol bits, the brain chosen by the AI, door passage and legs.
class TestCreatureParts : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

    int type(const QString &name) const { return *m_content->mobIndex(u"qtcraft:"_s + name); }

    // Routes that give one leg and count the questions.
    class OneLeg : public QcMobLegs::Routes
    {
    public:
        mutable int asked = 0;
        int reach() const override { return 24; }
        std::optional<QcBlockPos> nextLeg(QcBlockPos, QcBlockPos) const override
        {
            ++asked;
            return QcBlockPos{20, 1, 0};
        }
        std::optional<QcBlockPos> wayOutOfWater(QcBlockPos) const override
        {
            ++asked;
            return QcBlockPos{0, 1, 5};
        }
    };

private slots:
    void onlyVillagersCarryTheVillagerComponent()
    {
        QcMobManager mobs(m_content, 3);
        mobs.spawn(type(u"villager"_s), {0.5, 64.0, 0.5}, 1, false, false);
        mobs.spawn(type(u"zombie"_s), {4.5, 64.0, 0.5}, 2, false, false);
        QVERIFY(mobs.find(1)->isVillager());
        QCOMPARE(mobs.find(1)->villager->profession, m_content->villagerSettingsOf(type(u"villager"_s)).unemployed);
        QVERIFY(!mobs.find(2)->isVillager());

        // A copy is deep: changing it leaves the original as it was.
        QcCreature copy = *mobs.find(1);
        copy.villager->profession = u"qtcraft:farmer"_s;
        QVERIFY(mobs.find(1)->villager->profession != copy.villager->profession);
        QcCreature moved = std::move(copy);
        QCOMPARE(moved.villager->profession, u"qtcraft:farmer"_s);
        copy = *mobs.find(2);
        QVERIFY(!copy.isVillager());
    }

    void thePoseDecidesBoxEyesAndFlags()
    {
        const QcVec3 feet{10.5, 64.0, 20.5};
        QCOMPARE(QcPoses::boxAt(QcPose::Standing, feet, 90.0f, 0.6, 1.9), QcPhysics::boxAt(feet, 0.6, 1.9));
        QCOMPARE(QcPoses::boxAt(QcPose::Sitting, feet, 90.0f, 0.6, 1.9), QcPhysics::boxAt(feet, 0.6, 1.9));
        QCOMPARE(QcPoses::boxAt(QcPose::Lying, feet, 90.0f, 0.6, 1.9), QcPhysics::lyingBoxAt(feet, 90.0f, 0.6, 1.9));
        QCOMPARE(QcPoses::eyesAt(QcPose::Standing, feet, 0.0f, 0.6, 2.0).y, 64.0 + 2.0 * QcPoses::eyeShare);
        QVERIFY(QcPoses::eyesAt(QcPose::Lying, feet, 0.0f, 0.6, 2.0).y < 64.5);

        for (const QcPose pose : {QcPose::Standing, QcPose::Lying, QcPose::Sitting}) {
            QCOMPARE(QcMsg::poseOf(quint16(QcMsg::poseFlags(pose) | QcMsg::OnGround | QcMsg::Young)), pose);
        }
        QCOMPARE(QcMsg::poseFlags(QcPose::Lying), quint16(512));
    }

    void theAiChoosesTheBrain()
    {
        QRandomGenerator random(1);
        const QcMobBehaviour behaviour(m_content, random);
        QVERIFY(dynamic_cast<QcHostileBrain *>(&behaviour.brainOf(type(u"zombie"_s))));
        QVERIFY(dynamic_cast<QcVillagerBehaviour *>(&behaviour.brainOf(type(u"villager"_s))));
        QcMobBrain &pig = behaviour.brainOf(type(u"pig"_s));
        QVERIFY(dynamic_cast<QcPassiveBrain *>(&pig) && !dynamic_cast<QcHostileBrain *>(&pig));
        QVERIFY(behaviour.brainOf(type(u"villager"_s)).thinksInIdleCrowd());
        QVERIFY(!pig.thinksInIdleCrowd());
    }

    void aDoorIsOpenedOnTheWayAndClosedBehind()
    {
        QcCreature mob;
        mob.motion.position = {0.5, 1.0, 0.5};
        std::optional<QcBlockPos> opened;
        QcMobNavigator::Navigation navigation;
        navigation.door = QcBlockPos{1, 1, 0};
        QcDoorPassage::Moves moves = QcDoorPassage::use(mob, opened, navigation);
        QVERIFY(!moves.close);
        QCOMPARE(moves.open, navigation.door);
        QCOMPARE(opened, navigation.door);

        // Through it and on: closed behind; a door out of reach is not opened.
        mob.motion.position = {3.5, 1.0, 0.5};
        navigation.door = QcBlockPos{9, 1, 0};
        moves = QcDoorPassage::use(mob, opened, navigation);
        QCOMPARE(moves.close, std::optional(QcBlockPos{1, 1, 0}));
        QVERIFY(!moves.open && !opened);
    }

    void farGoalsGoInLegsWithinTheBudget()
    {
        QcCreature mob;
        mob.motion.position = {0.5, 1.0, 0.5};
        mob.motion.onGround = true;
        QcMobLegs::State state;
        const OneLeg routes;
        QcMobLegs::Budget budget{0, 1};
        QCOMPARE(QcMobLegs::towards(mob, state, {10, 1, 0}, routes, budget), (QcBlockPos{10, 1, 0}));
        QCOMPARE(routes.asked, 0);
        QCOMPARE(QcMobLegs::towards(mob, state, {100, 1, 0}, routes, budget), (QcBlockPos{20, 1, 0}));
        QCOMPARE(routes.asked, 1);
        QVERIFY(budget.spent());

        // Swimming where a search found no path: the shore first, once the budget allows.
        mob.motion.inWater = true;
        mob.path.planned = true;
        QCOMPARE(QcMobLegs::towards(mob, state, {100, 1, 0}, routes, budget), (QcBlockPos{20, 1, 0}));
        budget = {0, 1};
        QCOMPARE(QcMobLegs::towards(mob, state, {100, 1, 0}, routes, budget), (QcBlockPos{0, 1, 5}));
        QVERIFY(!state.leg);
        mob.motion.inWater = false;
        QVERIFY(!QcMobLegs::wayOutOfWater(mob, state, routes, budget));
    }
};

QTEST_GUILESS_MAIN(TestCreatureParts)
#include "test_creature_parts.moc"
