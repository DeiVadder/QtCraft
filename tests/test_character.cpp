#include "QcAnimator.h"
#include "QcEntityMesher.h"
#include "QcPhysics.h"
#include "QcTestWorld.h"
#include "QcViewCamera.h"

#include <QTest>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

QcModelDefinition humanoid()
{
    QcModelDefinition model;
    model.name = u"t:humanoid"_s;
    model.bones = {
        {u"body"_s, QcBoneRole::Body, {0.0, 0.7, 0.0}, {{{-0.25, 0.7, -0.125}, {0.25, 1.35, 0.125}, u"torso"_s}}},
        {u"head"_s, QcBoneRole::Head, {0.0, 1.35, 0.0}, {{{-0.225, 1.35, -0.225}, {0.225, 1.8, 0.225}, u"head"_s}}},
        {u"right_arm"_s, QcBoneRole::RightArm, {0.35, 1.3, 0.0}, {{{0.25, 0.7, -0.1}, {0.45, 1.35, 0.1}, u"arm"_s}}},
        {u"left_arm"_s, QcBoneRole::LeftArm, {-0.35, 1.3, 0.0}, {{{-0.45, 0.7, -0.1}, {-0.25, 1.35, 0.1}, u"arm"_s}}},
        {u"right_leg"_s, QcBoneRole::RightLeg, {0.125, 0.7, 0.0}, {{{0.0, 0.0, -0.125}, {0.25, 0.7, 0.125}, u"legs"_s}}},
        {u"left_leg"_s, QcBoneRole::LeftLeg, {-0.125, 0.7, 0.0}, {{{-0.25, 0.0, -0.125}, {0.0, 0.7, 0.125}, u"legs"_s}}},
    };
    return model;
}

QcAppearanceDefinition outfit()
{
    QcAppearanceDefinition definition;
    definition.name = u"t:outfit"_s;
    definition.layers = {{u"torso"_s, u"t:cloth"_s, QcTint::Shirt},
                         {u"head"_s, u"t:skin"_s, QcTint::Skin},
                         {u"arm"_s, u"t:skin"_s, QcTint::Skin},
                         {u"legs"_s, u"t:cloth"_s, QcTint::Pants}};
    return definition;
}

QRectF anyUv(const QString &)
{
    return {0.0, 0.0, 1.0, 1.0};
}

// Lowest y of the vertices whose tint matches.
double lowestY(const QcEntityMesh &mesh)
{
    double lowest = 1e9;
    for (const QcEntityVertex &vertex : mesh.vertices) {
        lowest = std::min(lowest, double(vertex.y));
    }
    return lowest;
}

} // namespace

// Skeleton animation, model meshing, the first-person hand and the third-person camera.
class TestCharacter : public QObject
{
    Q_OBJECT

private slots:
    void standingStillBarelyMoves()
    {
        const QcAnimator::BodyPose pose = QcAnimator::animateBody({}, 1.0);
        QCOMPARE(pose.bone(QcBoneRole::RightLeg).pitch, 0.0);
        QCOMPARE(pose.bone(QcBoneRole::LeftLeg).pitch, 0.0);
        QVERIFY(std::abs(pose.bone(QcBoneRole::RightArm).roll) < 8.0);
        QCOMPARE(pose.bodyPitch, 0.0);
        QCOMPARE(pose.bodyRoll, 0.0);
    }

    void walkingSwingsLimbsAgainstEachOther()
    {
        QcEntityPose walking;
        walking.speed = QcPhysics::walkSpeed;
        walking.walkDistance = 0.3;
        const QcAnimator::BodyPose pose = QcAnimator::animateBody(walking, 0.0);
        const double leg = pose.bone(QcBoneRole::RightLeg).pitch;
        QVERIFY(std::abs(leg) > 20.0);
        QCOMPARE(pose.bone(QcBoneRole::LeftLeg).pitch, -leg);
        // Each arm swings against the leg on its side.
        QVERIFY(pose.bone(QcBoneRole::RightArm).pitch * leg < 0.0);
        // Sprinting swings further; a slow walk less.
        walking.sprinting = true;
        QVERIFY(std::abs(QcAnimator::animateBody(walking, 0.0).bone(QcBoneRole::RightLeg).pitch) > std::abs(leg));
        walking.sprinting = false;
        walking.speed = QcPhysics::walkSpeed * 0.25;
        QVERIFY(std::abs(QcAnimator::animateBody(walking, 0.0).bone(QcBoneRole::RightLeg).pitch) < std::abs(leg));
    }

    void actionsAndStatesHaveTheirPoses()
    {
        QcEntityPose pose;
        pose.swingProgress = 0.5;
        QVERIFY(QcAnimator::animateBody(pose, 0.0).bone(QcBoneRole::RightArm).pitch > 90.0);
        QCOMPARE(QcAnimator::animateBody(pose, 0.0).bone(QcBoneRole::LeftArm).pitch, 0.0);

        QcEntityPose swimming;
        swimming.inWater = true;
        swimming.onGround = false;
        swimming.speed = QcPhysics::swimSpeed;
        const QcAnimator::BodyPose swim = QcAnimator::animateBody(swimming, 0.0);
        QVERIFY(swim.bodyPitch > 45.0);
        QVERIFY(swim.bone(QcBoneRole::RightArm).pitch > 100.0);

        QcEntityPose jumping;
        jumping.onGround = false;
        QVERIFY(QcAnimator::animateBody(jumping, 0.0).bone(QcBoneRole::RightArm).roll > 10.0);

        QcEntityPose sneaking;
        sneaking.sneaking = true;
        QVERIFY(QcAnimator::animateBody(sneaking, 0.0).bodyLift < 0.0);

        QcEntityPose zombie;
        zombie.armsForward = true;
        QCOMPARE(QcAnimator::animateBody(zombie, 0.0).bone(QcBoneRole::LeftArm).pitch, 90.0);

        QcEntityPose looking;
        looking.headPitch = 30.0f;
        QCOMPARE(QcAnimator::animateBody(looking, 0.0).bone(QcBoneRole::Head).pitch, 30.0);

        QcEntityPose hurt;
        hurt.hurtProgress = 0.5;
        QVERIFY(QcAnimator::animateBody(hurt, 0.0).bodyPitch < 0.0);

        QcEntityPose dying;
        dying.deathProgress = 0.0;
        QCOMPARE(QcAnimator::animateBody(dying, 0.0).bodyRoll, 0.0);
        dying.deathProgress = 1.0;
        QCOMPARE(QcAnimator::animateBody(dying, 0.0).bodyRoll, 90.0);
    }

    void handFollowsActions()
    {
        const QcEntityPose idle;
        const QcAnimator::HandPose rest = QcAnimator::animateHand(idle, 0.0, -1.0, -1.0, -1.0);
        QVERIFY(rest.offset.length() < 0.01);
        // Switching items lowers the hand most in the middle of the switch.
        QVERIFY(QcAnimator::animateHand(idle, 0.0, 0.5, -1.0, -1.0).offset.y < -0.3);
        QVERIFY(QcAnimator::animateHand(idle, 0.0, 1.0, -1.0, -1.0).offset.y > -0.01);
        QcEntityPose swinging;
        swinging.swingProgress = 0.5;
        const QcAnimator::HandPose swing = QcAnimator::animateHand(swinging, 0.0, -1.0, -1.0, -1.0);
        QVERIFY(swing.pitch > 30.0);
        QVERIFY(swing.offset.z < -0.1);
        QVERIFY(QcAnimator::animateHand(idle, 0.0, -1.0, -1.0, 0.5).offset.z < -0.05);
        QVERIFY(QcAnimator::animateHand(idle, 0.0, -1.0, 0.5, -1.0).offset.y < -0.02);
        QcEntityPose walking;
        walking.speed = QcPhysics::walkSpeed;
        walking.walkDistance = 0.3;
        QVERIFY(QcAnimator::animateHand(walking, 0.0, -1.0, -1.0, -1.0).offset.length() > 0.01);
    }

    void modelsAreMeshedWithTintedLayers()
    {
        const QcModelDefinition model = humanoid();
        QcAppearanceDefinition look = outfit();
        QcEntityMesher::ModelLook modelLook{&model, &look, nullptr, {}, 1.0};
        modelLook.colors.shirtColor = 0x102030;
        QcEntityMesh mesh;
        const QcEntityMesher::Placement placement{{10.0, 64.0, 5.0}, 0.0, 1.0f, false};
        QcEntityMesher::appendModel(mesh, {modelLook, nullptr}, QcAnimator::animateBody({}, 0.0), placement, anyUv);
        // Six boxes of six faces.
        QCOMPARE(mesh.indices.size(), 6 * 6 * 6);
        bool shirt = false;
        for (const QcEntityVertex &vertex : std::as_const(mesh.vertices)) {
            shirt = shirt || vertex.tint == std::array<quint8, 4>{0x10, 0x20, 0x30, 255};
        }
        QVERIFY(shirt);
        QCOMPARE(lowestY(mesh), 64.0);
        // Slots without a layer are left out; a hurt entity is tinted red.
        look.layers.removeLast();
        QcEntityMesh hurt;
        QcEntityMesher::appendModel(hurt, {modelLook, nullptr}, QcAnimator::animateBody({}, 0.0), {{0.0, 0.0, 0.0}, 0.0, 1.0f, true}, anyUv);
        QCOMPARE(hurt.indices.size(), 4 * 6 * 6);
        QVERIFY(std::ranges::all_of(hurt.vertices, [](const QcEntityVertex &v) { return v.tint[1] < 255; }));
        // Twice the scale, twice the height.
        modelLook.scale = 2.0;
        QcEntityMesh big;
        QcEntityMesher::appendModel(big, {modelLook, nullptr}, QcAnimator::animateBody({}, 0.0), placement, anyUv);
        double top = 0.0;
        for (const QcEntityVertex &vertex : std::as_const(big.vertices)) {
            top = std::max(top, double(vertex.y));
        }
        QVERIFY(std::abs(top - (64.0 + 3.6)) < 1e-4);
    }

    void hairFollowsTheHeadAndHeldItemsAreDrawn()
    {
        const QcModelDefinition model = humanoid();
        const QcAppearanceDefinition look = outfit();
        QcAppearanceDefinition hair;
        hair.name = u"t:hair"_s;
        hair.kind = QcAppearanceDefinition::Kind::Hair;
        hair.layers = {{u"hair"_s, u"t:hair"_s, QcTint::Hair}};
        hair.boxes = {{{-0.235, 1.66, -0.235}, {0.235, 1.82, 0.235}, u"hair"_s}};
        QcEntityMesher::ModelLook modelLook{&model, &look, &hair, {}, 1.0};
        QcEntityMesh plain;
        QcEntityMesher::appendModel(plain, {modelLook, nullptr}, QcAnimator::animateBody({}, 0.0), {}, anyUv);
        QCOMPARE(plain.indices.size(), 7 * 6 * 6);
        // Looking down tilts the hair forward with the head.
        QcEntityPose down;
        down.headPitch = -60.0f;
        QcEntityMesh tilted;
        QcEntityMesher::appendModel(tilted, {modelLook, nullptr}, QcAnimator::animateBody(down, 0.0), {}, anyUv);
        const auto minZ = [](const QcEntityMesh &mesh, qsizetype firstVertex) {
            double z = 1e9;
            for (qsizetype i = firstVertex; i < mesh.vertices.size(); ++i) {
                z = std::min(z, double(mesh.vertices[i].z));
            }
            return z;
        };
        const qsizetype hairStart = plain.vertices.size() - 24;
        QVERIFY(minZ(tilted, hairStart) < minZ(plain, hairStart) - 0.1);
        QcEntityMesher::ItemLook sword;
        sword.sprite = u"t:sword"_s;
        QcEntityMesh armed;
        QcEntityMesher::appendModel(armed, {modelLook, &sword}, QcAnimator::animateBody({}, 0.0), {}, anyUv);
        // Two crossed, double-sided sprites.
        QCOMPARE(armed.indices.size() - plain.indices.size(), 4 * 6);
    }

    void handIsDrawnInFrontOfTheCamera()
    {
        const QcModelDefinition model = humanoid();
        const QcAppearanceDefinition look = outfit();
        const QcEntityMesher::ModelLook modelLook{&model, &look, nullptr, {}, 1.0};
        QcEntityMesh mesh;
        const QcVec3 eye{0.0, 70.0, 0.0};
        QcEntityMesher::appendHand(mesh, {modelLook, nullptr}, {}, {eye, 0.0, 0.0, 1.0f}, anyUv);
        QCOMPARE(mesh.indices.size(), 6 * 6);
        // Looking north (-Z): the arm lies ahead, to the right and below the eyes, within reach.
        QcVec3 centre;
        for (const QcEntityVertex &vertex : std::as_const(mesh.vertices)) {
            centre += QcVec3{vertex.x, vertex.y, vertex.z} * (1.0 / double(mesh.vertices.size()));
        }
        QVERIFY(centre.z < 0.0);
        QVERIFY(centre.x > 0.0);
        QVERIFY(centre.y < eye.y);
        QVERIFY((centre - eye).length() < 1.0);
        // Turned to face east (+X), the arm turns with the view.
        QcEntityMesh east;
        QcEntityMesher::appendHand(east, {modelLook, nullptr}, {}, {eye, 90.0, 0.0, 1.0f}, anyUv);
        QcVec3 eastCentre;
        for (const QcEntityVertex &vertex : std::as_const(east.vertices)) {
            eastCentre += QcVec3{vertex.x, vertex.y, vertex.z} * (1.0 / double(east.vertices.size()));
        }
        QVERIFY(eastCentre.x > 0.0);
        QVERIFY(eastCentre.z > 0.0);
        QcEntityMesher::ItemLook block;
        block.cube = true;
        QcEntityMesh holding;
        QcEntityMesher::appendHand(holding, {modelLook, &block}, {}, {eye, 0.0, 0.0, 1.0f}, anyUv);
        QCOMPARE(holding.indices.size(), 2 * 6 * 6);
    }

    void glowingItemsAreNeverDarkerThanTheirOwnLight()
    {
        const QcModelDefinition model = humanoid();
        const QcAppearanceDefinition look = outfit();
        const QcEntityMesher::ModelLook modelLook{&model, &look, nullptr, {}, 1.0};
        QcEntityMesher::ItemLook torch;
        torch.sprite = u"t:torch"_s;
        torch.glow = 14.0f / 15.0f;
        const QcEntityLight dark(0.0f, 0.1f);
        const QcVec3 eye{0.0, 70.0, 0.0};
        // In the hand: the arm takes the light around it, the torch its own.
        QcEntityMesh hand;
        QcEntityMesher::appendHand(hand, {modelLook, nullptr}, {}, {eye, 0.0, 0.0, dark}, anyUv);
        const qsizetype armVertices = hand.vertices.size();
        QcEntityMesher::appendHand(hand, {modelLook, &torch}, {}, {eye, 0.0, 0.0, dark}, anyUv);
        QCOMPARE(hand.vertices[0].blockLight, 0.1f);
        QVERIFY(hand.vertices.size() > 2 * armVertices);
        for (qsizetype i = 2 * armVertices; i < hand.vertices.size(); ++i) {
            QCOMPARE(hand.vertices[i].blockLight, torch.glow);
            QCOMPARE(hand.vertices[i].skyLight, 0.0f);
        }
        // Brighter surroundings win over the glow.
        QcEntityMesh lit;
        QcEntityMesher::appendHand(lit, {modelLook, &torch}, {}, {eye, 0.0, 0.0, QcEntityLight(0.0f, 1.0f)}, anyUv);
        QCOMPARE(lit.vertices.last().blockLight, 1.0f);
        // Dropped and held by a creature in the third person, as a sprite and as a block.
        QcClientEntity dropped;
        dropped.item = {1, 1, 0};
        QcEntityMesh lying;
        QcEntityMesher::appendItem(lying, dropped, torch, {1.0, 0.0, dark}, anyUv);
        QVERIFY(!lying.vertices.isEmpty());
        QVERIFY(std::ranges::all_of(lying.vertices, [&](const QcEntityVertex &vertex) { return vertex.blockLight == torch.glow; }));
        QcEntityMesher::ItemLook lantern;
        lantern.cube = true;
        lantern.glow = 11.0f / 15.0f;
        QcEntityMesh held;
        QcEntityMesher::appendModel(held, {modelLook, &lantern}, QcAnimator::animateBody({}, 0.0), {{}, 0.0, dark, false}, anyUv);
        QCOMPARE(held.vertices.first().blockLight, 0.1f);
        QCOMPARE(held.vertices.last().blockLight, lantern.glow);
        // A sprite in the hand is about as large as a held block: its diagonal is at most that of
        // the held cube (0.15 m edge, 0.26 m diagonal).
        QcEntityMesh sprite;
        QcEntityMesher::appendHand(sprite, {modelLook, &torch}, {}, {eye, 0.0, 0.0, dark}, anyUv);
        double diagonal = 0.0;
        for (qsizetype i = armVertices; i < sprite.vertices.size(); ++i) {
            for (qsizetype j = armVertices; j < sprite.vertices.size(); ++j) {
                const QcEntityVertex &a = sprite.vertices[i];
                const QcEntityVertex &b = sprite.vertices[j];
                diagonal = std::max(diagonal, QcVec3{a.x - b.x, a.y - b.y, a.z - b.z}.length());
            }
        }
        QVERIFY2(diagonal > 0.15 && diagonal < 0.27, qPrintable(QString::number(diagonal)));
    }

    void thirdPersonCameraAvoidsWalls()
    {
        QcContentRegistry content = qcMakeTestContent();
        QcBlockDefinition flower;
        flower.name = u"test:flower"_s;
        flower.solid = false;
        flower.opaque = false;
        flower.renderLayer = QcRenderLayer::Cutout;
        flower.faceTextures.fill(u"test:flower"_s);
        QVERIFY(content.registerBlock(flower).has_value());
        QcTestWorld world(content);
        const QcVec3 eye{0.5, 70.0, 0.5};
        const QcViewCamera::Placement first = QcViewCamera::place(eye, 0.0f, 0.0f, QcCameraMode::FirstPerson, world);
        QCOMPARE(first.position, eye);
        // Open space: four metres behind (south, +Z), looking the same way.
        const QcViewCamera::Placement behind = QcViewCamera::place(eye, 0.0f, 0.0f, QcCameraMode::Behind, world);
        QCOMPARE(behind.position.z, eye.z + QcViewCamera::thirdPersonDistance);
        QCOMPARE(behind.yaw, 0.0f);
        // In front, turned around and with the pitch mirrored.
        const QcViewCamera::Placement front = QcViewCamera::place(eye, 0.0f, 20.0f, QcCameraMode::Front, world);
        QVERIFY(front.position.z < eye.z);
        QCOMPARE(front.yaw, 180.0f);
        QCOMPARE(front.pitch, -20.0f);
        // Plants do not stop the camera; it stays at the full distance behind them.
        world.fillBox({-3, 69, 1}, {3, 71, 1}, *content.blockId(u"test:flower"_s));
        const QcViewCamera::Placement throughPlants = QcViewCamera::place(eye, 0.0f, 0.0f, QcCameraMode::Behind, world);
        QCOMPARE(throughPlants.position.z, eye.z + QcViewCamera::thirdPersonDistance);
        // A wall two blocks behind pulls the camera in front of it.
        world.fillBox({-3, 65, 2}, {3, 75, 2}, *content.blockId(u"test:stone"_s));
        const QcViewCamera::Placement blocked = QcViewCamera::place(eye, 0.0f, 0.0f, QcCameraMode::Behind, world);
        QVERIFY(blocked.position.z < 2.0);
        QVERIFY(blocked.position.z > eye.z);
        QCOMPARE(QcViewCamera::next(QcCameraMode::FirstPerson), QcCameraMode::Behind);
        QCOMPARE(QcViewCamera::next(QcCameraMode::Front), QcCameraMode::FirstPerson);
        QCOMPARE(QcViewCamera::fromName(u"front"_s), std::optional<QcCameraMode>(QcCameraMode::Front));
        QVERIFY(!QcViewCamera::fromName(u"sideways"_s));
    }
};

QTEST_GUILESS_MAIN(TestCharacter)
#include "test_character.moc"
