#include "clientcore/meshing/QcAnimator.h"
#include "clientcore/meshing/QcEntityMesher.h"
#include "clientcore/meshing/QcFigureLook.h"
#include "clientcore/preview/QcPreviewActivity.h"
#include "clientcore/preview/QcPreviewCamera.h"
#include "clientcore/preview/QcPreviewHook.h"
#include "clientcore/preview/QcPreviewPacer.h"
#include "clientcore/preview/QcPreviewPose.h"
#include "clientcore/preview/QcPreviewScene.h"
#include "clientcore/session/QcClientEntities.h"
#include "common/content/loader/QcBaseContent.h"
#include "common/core/QcCrc32.h"
#include "common/physics/QcPhysics.h"

#include <QDataStream>
#include <QIODevice>
#include <QTest>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

QRectF anyUv(const QString &)
{
    return {0.0, 0.0, 1.0, 1.0};
}

// Mean x of the vertices above `minY` (the head of a 1.8 m figure lies above 1.35 m).
double meanX(const QcEntityMesh &mesh, double minY, double maxY = 10.0)
{
    double sum = 0.0;
    int count = 0;
    for (const QcEntityVertex &vertex : mesh.vertices) {
        if (vertex.y > minY && vertex.y < maxY) {
            sum += vertex.x;
            ++count;
        }
    }
    return count > 0 ? sum / count : 0.0;
}

// Frames the pacer allows while a timer ticks every `tickMs` for `durationMs`.
int framesDrawn(QcPreviewPacer &pacer, int tickMs, int durationMs, bool visible = true)
{
    int frames = 0;
    for (int now = 0; now < durationMs; now += tickMs) {
        frames += pacer.frameDue(visible, now) ? 1 : 0;
    }
    return frames;
}

// Vertex count and a CRC over indices, positions (to 0.1 mm), texture coordinates, light and tints:
// equal for equal meshes, whatever rounding the build's optimiser does in the last bits.
std::pair<qsizetype, quint32> fingerprint(const QcEntityMesh &mesh)
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    for (const quint32 index : mesh.indices) {
        stream << index;
    }
    const auto round = [](float value, double scale) { return qint64(std::llround(double(value) * scale)); };
    for (const QcEntityVertex &v : mesh.vertices) {
        stream << round(v.x, 1e4) << round(v.y, 1e4) << round(v.z, 1e4) << round(v.u, 1e5) << round(v.v, 1e5) << round(v.shade, 1e4)
               << round(v.skyLight, 1e4) << round(v.blockLight, 1e4) << v.tint[0] << v.tint[1] << v.tint[2] << v.tint[3];
    }
    return {mesh.vertices.size(), qcCrc32(bytes)};
}

QList<QcEntityPose> worldPoses()
{
    QcEntityPose walk;
    walk.speed = 4.3;
    walk.walkDistance = 1.3;
    walk.headPitch = 20.0f;
    QcEntityPose swing;
    swing.swingProgress = 0.3;
    swing.sneaking = true;
    swing.headPitch = -30.0f;
    QcEntityPose death;
    death.deathProgress = 0.5;
    QcEntityPose hurt;
    hurt.hurtProgress = 0.4;
    hurt.inWater = true;
    hurt.onGround = false;
    hurt.armsForward = true;
    return {QcEntityPose(), walk, swing, death, hurt};
}

QcPreviewActivity::State shownState(QcPreviewActivity::Window window)
{
    return {true, true, true, window, false, false};
}

} // namespace

// The character preview without a window (decision E-231): camera, poses and gaze, frame pacing,
// the scene against the world's meshing, and the --preview test hook.
class TestCharacterPreview : public QObject
{
    Q_OBJECT

private slots:
    void cameraTurnsZoomsAndShowsSides()
    {
        QcPreviewCamera camera;
        QCOMPARE(camera.side(), QcPreviewCamera::Side::Front);
        QCOMPARE(camera.modelYaw(), 155.0);

        // Dragging 20 px to the right turns the face 10 degrees towards +X (yaw 90 faces +X).
        camera.show(QcPreviewCamera::Side::Front);
        QCOMPARE(camera.modelYaw(), 180.0);
        camera.drag(20.0);
        QCOMPARE(camera.modelYaw(), 170.0);
        // Turned more than 90 degrees away, the back faces the camera; angles wrap into [0, 360).
        camera.drag(200.0);
        QCOMPARE(camera.modelYaw(), 70.0);
        QCOMPARE(camera.side(), QcPreviewCamera::Side::Back);
        camera.drag(200.0);
        QCOMPARE(camera.modelYaw(), 330.0);
        camera.show(QcPreviewCamera::Side::Back);
        QCOMPARE(camera.modelYaw(), 0.0);
        QCOMPARE(camera.side(), QcPreviewCamera::Side::Back);

        // One notch zooms by 0.12, clamped to 0..1; full zoom looks at the head from 1.5 m.
        camera.wheel(120);
        QVERIFY(std::abs(camera.zoom() - 0.12) < 1e-9);
        camera.wheel(-240);
        QCOMPARE(camera.zoom(), 0.0);
        QCOMPARE(camera.distance(), 4.4);
        QCOMPARE(camera.camera().position, QVector3D(0.0f, 0.92f, 4.4f));
        camera.setZoom(7.0);
        QCOMPARE(camera.zoom(), 1.0);
        QCOMPARE(camera.camera().position, QVector3D(0.0f, 1.55f, 1.5f));
        camera.setZoom(std::nan(""));
        QCOMPARE(camera.zoom(), 1.0);

        camera.reset();
        QCOMPARE(camera.zoom(), 0.0);
        QCOMPARE(camera.modelYaw(), 155.0);
    }

    void wholeFigureFitsTheView()
    {
        // At zoom 0 the camera sees from the feet to above the head (vertical field of view 30°).
        const QcPreviewCamera camera;
        const double halfHeight = camera.distance() * std::tan(qDegreesToRadians(15.0));
        QVERIFY(camera.target().y - halfHeight < 0.0);
        QVERIFY(camera.target().y + halfHeight > 1.8);
    }

    void animationsHaveTheirPoses()
    {
        const QcEntityPose stand = QcPreviewPose::pose(QcPreviewPose::Animation::Stand, 1.0, {});
        QCOMPARE(stand.speed, 0.0);
        QVERIFY(stand.swingProgress < 0.0);
        QVERIFY(stand.onGround);

        // Walking on the spot: walking speed, the stride advances with the time.
        const QcEntityPose walk = QcPreviewPose::pose(QcPreviewPose::Animation::Walk, 2.0, {});
        QCOMPARE(walk.speed, QcPhysics::walkSpeed);
        QCOMPARE(walk.walkDistance, 2.0 * QcPhysics::walkSpeed);
        // A quarter stride (0.3125 m) swings the legs furthest.
        const double quarterStride = 0.3125 / QcPhysics::walkSpeed;
        const QcAnimator::BodyPose striding = QcAnimator::animateBody(QcPreviewPose::pose(QcPreviewPose::Animation::Walk, quarterStride, {}), 0.0);
        QVERIFY(std::abs(striding.bone(QcBoneRole::RightLeg).pitch) > 10.0);

        // Mining: a 0.3 s swing every 0.5 s.
        QCOMPARE(QcPreviewPose::pose(QcPreviewPose::Animation::Mine, 0.15, {}).swingProgress, 0.5);
        QVERIFY(QcPreviewPose::pose(QcPreviewPose::Animation::Mine, 0.4, {}).swingProgress < 0.0);
        QVERIFY(std::abs(QcPreviewPose::pose(QcPreviewPose::Animation::Mine, 0.65, {}).swingProgress - 0.5) < 1e-9);

        QCOMPARE(QcPreviewPose::fromName(u"walk"_s), QcPreviewPose::Animation::Walk);
        QCOMPARE(QcPreviewPose::name(QcPreviewPose::Animation::Mine), u"mine"_s);
        QVERIFY(!QcPreviewPose::fromName(u"dance"_s));
    }

    void headFollowsThePointer()
    {
        QcPreviewCamera camera;
        camera.show(QcPreviewCamera::Side::Front);

        // The pointer at the camera's eye height in the centre: about straight ahead.
        const QcPreviewPose::Gaze ahead = QcPreviewPose::gazeAt(camera, 0.0, 0.0, 1.0);
        QVERIFY(std::abs(ahead.yaw) < 1e-6);
        // Facing the camera, the screen's right is the figure's left (negative yaw), clearly
        // turned; a pointer further up lifts the head.
        const QcPreviewPose::Gaze right = QcPreviewPose::gazeAt(camera, 1.0, 1.0, 1.0);
        QVERIFY2(right.yaw < -15.0, qPrintable(QString::number(right.yaw)));
        QVERIFY(right.pitch > QcPreviewPose::gazeAt(camera, 1.0, 0.0, 1.0).pitch + 5.0);
        QVERIFY(QcPreviewPose::gazeAt(camera, -1.0, 0.0, 1.0).yaw > 15.0);
        // Limits: never further than 60 degrees aside and 35 up or down.
        const QcPreviewPose::Gaze wide = QcPreviewPose::gazeAt(camera, 1.0, -1.0, 8.0);
        QVERIFY(wide.yaw >= -QcPreviewPose::maxGazeYaw && wide.yaw < -20.0);
        QVERIFY(wide.pitch >= -QcPreviewPose::maxGazePitch);

        // Seen from behind, the head does not twist round towards the viewer.
        camera.show(QcPreviewCamera::Side::Back);
        const QcPreviewPose::Gaze behind = QcPreviewPose::gazeAt(camera, 0.0, 0.0, 1.0);
        QVERIFY(std::abs(behind.yaw) < 1e-6);

        // The head moves towards the target smoothly and never past it.
        QcPreviewPose::Gaze head;
        const QcPreviewPose::Gaze target{40.0, -10.0};
        head = QcPreviewPose::approach(head, target, 0.05);
        QVERIFY(head.yaw > 0.0 && head.yaw < 40.0);
        for (int i = 0; i < 100; ++i) {
            head = QcPreviewPose::approach(head, target, 0.1);
        }
        QVERIFY(std::abs(head.yaw - 40.0) < 1e-3);
        QCOMPARE(QcPreviewPose::approach(head, target, 0.0), head);
    }

    void pacerDrawsOnlyWhileVisibleAndLimitsTheRate()
    {
        QcPreviewPacer pacer;
        QCOMPARE(pacer.intervalMs(), 33);
        // A timer ticking every millisecond for a second: 30 intervals (31 frames from 0 to
        // 999 ms), not 1000.
        const int frames = framesDrawn(pacer, 1, 1000);
        QVERIFY2(frames >= 30 && frames <= 31, qPrintable(QString::number(frames)));
        // The real timer ticks at the interval: one frame per tick, 30 a second.
        QcPreviewPacer timed;
        QCOMPARE(framesDrawn(timed, timed.intervalMs(), 990), 30);
        // A tick a millisecond early still draws; two milliseconds early waits.
        QcPreviewPacer early;
        QVERIFY(early.frameDue(true, 0));
        QVERIFY(early.frameDue(true, 32));
        QVERIFY(!early.frameDue(true, 64));

        // Hidden: nothing, however often asked.
        QcPreviewPacer hidden;
        QCOMPARE(framesDrawn(hidden, 1, 1000, false), 0);

        // The first frame after becoming visible comes at once, even right after the previous one.
        QcPreviewPacer pacer2;
        QVERIFY(pacer2.frameDue(true, 100));
        QVERIFY(!pacer2.frameDue(true, 110));
        QVERIFY(!pacer2.frameDue(false, 111));
        QVERIFY(pacer2.frameDue(true, 112));

        pacer2.setFramesPerSecond(1000);
        QCOMPARE(pacer2.framesPerSecond(), QcPreviewPacer::maxFramesPerSecond);
        pacer2.setFramesPerSecond(0);
        QCOMPARE(pacer2.framesPerSecond(), 1);
    }

    void previewAnimatesOnlyInTheActiveWindow()
    {
        using QcPreviewActivity::Mode;
        using QcPreviewActivity::Window;
        // Shown in the active window: animated, the timer runs.
        QCOMPARE(QcPreviewActivity::mode(shownState(Window::Active)), Mode::Animated);
        QVERIFY(QcPreviewActivity::timerRuns(Mode::Animated, true));

        // Another program has the focus (or a scripted run): the picture stays; the timer runs only
        // until a frame showed the figure (the atlas may still be loading).
        QCOMPARE(QcPreviewActivity::mode(shownState(Window::Inactive)), Mode::Still);
        QVERIFY(QcPreviewActivity::timerRuns(Mode::Still, false));
        QVERIFY(!QcPreviewActivity::timerRuns(Mode::Still, true));

        // Covered by another screen (the menu card under the character screen): the same.
        QcPreviewActivity::State paused = shownState(Window::Active);
        paused.paused = true;
        QCOMPARE(QcPreviewActivity::mode(paused), Mode::Still);

        // Minimised, hidden or not yet in a window, hidden itself (or a parent), without a size or
        // without a source: off, no timer at all.
        for (const Window window : {Window::Minimised, Window::Hidden, Window::None}) {
            QCOMPARE(QcPreviewActivity::mode(shownState(window)), Mode::Off);
        }
        QcPreviewActivity::State hidden = shownState(Window::Active);
        hidden.visible = false;
        QCOMPARE(QcPreviewActivity::mode(hidden), Mode::Off);
        QcPreviewActivity::State empty = shownState(Window::Active);
        empty.hasSize = false;
        QCOMPARE(QcPreviewActivity::mode(empty), Mode::Off);
        QcPreviewActivity::State sourceless = shownState(Window::Active);
        sourceless.hasSource = false;
        QCOMPARE(QcPreviewActivity::mode(sourceless), Mode::Off);
        QVERIFY(!QcPreviewActivity::timerRuns(Mode::Off, false));
        // Hidden wins over paused.
        hidden.paused = true;
        QCOMPARE(QcPreviewActivity::mode(hidden), Mode::Off);
    }

    void forcedActivityAnimatesOnlyAShownInactiveWindow()
    {
        using QcPreviewActivity::Mode;
        using QcPreviewActivity::Window;
        // QTCRAFT_PREVIEW_ACTIVE=1 (benchmarks): the inactive window of a scripted run animates.
        QcPreviewActivity::State forced = shownState(Window::Inactive);
        forced.forceActive = true;
        QCOMPARE(QcPreviewActivity::mode(forced), Mode::Animated);
        QcPreviewActivity::State active = shownState(Window::Active);
        active.forceActive = true;
        QCOMPARE(QcPreviewActivity::mode(active), Mode::Animated);

        // Paused (another screen on top) stays still, and nothing turns a hidden, minimised or
        // windowless preview on.
        QcPreviewActivity::State paused = forced;
        paused.paused = true;
        QCOMPARE(QcPreviewActivity::mode(paused), Mode::Still);
        for (const Window window : {Window::Minimised, Window::Hidden, Window::None}) {
            QcPreviewActivity::State state = shownState(window);
            state.forceActive = true;
            QCOMPARE(QcPreviewActivity::mode(state), Mode::Off);
        }
        QcPreviewActivity::State hidden = forced;
        hidden.visible = false;
        QCOMPARE(QcPreviewActivity::mode(hidden), Mode::Off);
    }

    void worldMeshWithoutHeadYawIsUnchanged()
    {
        QStringList warnings;
        const auto base = QcBaseContent::load(warnings);
        QVERIFY(base);
        const QcContentRegistry &content = **base;

        // The world's figures (head yaw 0) are drawn exactly as before the head could turn: these
        // fingerprints were taken from the meshes of the commit before stage 22 (78618f4).
        QcEntityMesher::ModelLook look;
        look.model = content.model(u"qtcraft:humanoid"_s);
        look.outfit = content.appearance(u"qtcraft:miner"_s);
        look.hair = content.appearance(u"qtcraft:cap"_s);
        look.colors.shirtColor = 0x123456;
        look.colors.skinColor = 0xb97b56;
        QcEntityMesher::ItemLook held;
        held.sprite = content.item(*content.itemId(u"qtcraft:iron_pickaxe"_s)).texture;
        const QList<quint32> before{0x29832d24, 0x06e75bdf, 0x269ee273, 0xe97c883a, 0xad64ea6f};

        const QList<QcEntityPose> poses = worldPoses();
        QCOMPARE(poses.size(), before.size());
        for (qsizetype i = 0; i < poses.size(); ++i) {
            QcEntityMesh mesh;
            QcEntityMesher::appendModel(mesh, {look, &held}, QcAnimator::animateBody(poses[i], 3.7),
                                        {{1.5, 64.0, -2.25}, 37.0, QcEntityLight(0.8f, 0.3f), false}, anyUv);
            const std::pair<qsizetype, quint32> print = fingerprint(mesh);
            QCOMPARE(print.first, 328);
            QVERIFY2(print.second == before[i], qPrintable(u"pose %1: 0x%2"_s.arg(i).arg(print.second, 8, 16, QChar(u'0'))));
        }
    }

    void othersArmourIsCheckedBeforeItIsDrawn()
    {
        QStringList warnings;
        const auto base = QcBaseContent::load(warnings);
        QVERIFY(base);
        const QcContentRegistry &content = **base;
        const QcItemId helmet = *content.itemId(u"qtcraft:iron_pickaxe"_s);

        // A player keeps what the content knows; an unknown item id is dropped (decision E-232).
        QcMsg::EntitySpawn player;
        player.id = 7;
        player.equipment.at(QcEquipmentSlot::Head) = {helmet, 1, 3};
        player.equipment.at(QcEquipmentSlot::Feet) = {QcItemId(60000), 1, 0};
        QcClientEntities entities;
        QVERIFY(entities.spawn(player, &content));
        QCOMPARE(entities.all().value(7).equipment.at(QcEquipmentSlot::Head), (QcItemStack{helmet, 1, 3}));
        QVERIFY(entities.all().value(7).equipment.at(QcEquipmentSlot::Feet).isEmpty());

        // Sent again (a new look): checked again.
        player.equipment.at(QcEquipmentSlot::Head) = {QcItemId(60001), 1, 0};
        QVERIFY(entities.spawn(player, &content));
        QVERIFY(entities.all().value(7).equipment.isEmpty());

        // Mobs and items wear nothing, whatever the server sends.
        QcMsg::EntitySpawn zombie;
        zombie.id = 8;
        zombie.kind = QcEntityKind::Creature;
        zombie.creatureType = 0;
        zombie.equipment.at(QcEquipmentSlot::Chest) = {helmet, 1, 0};
        QVERIFY(entities.spawn(zombie, &content));
        QVERIFY(entities.all().value(8).equipment.isEmpty());
    }

    void sceneIsTheWorldsFigure()
    {
        QStringList warnings;
        const auto base = QcBaseContent::load(warnings);
        QVERIFY2(base, qPrintable(base ? QString() : base.error()));
        const QcContentRegistry &content = **base;

        QcAppearance look;
        look.outfit = u"qtcraft:miner"_s;
        look.hair = u"qtcraft:cap"_s;
        look.shirtColor = 0x123456;
        const QcItemId pickaxe = *content.itemId(u"qtcraft:iron_pickaxe"_s);
        const QcEntityPose pose = QcPreviewPose::pose(QcPreviewPose::Animation::Mine, 0.1, {20.0, 5.0});
        const QcEntityMesh preview = QcPreviewScene::build(content, {look, pickaxe, pose, 155.0, 3.0}, anyUv);
        QVERIFY(!preview.indices.isEmpty());

        // Exactly what the world draws for another player with that look, pose and item in full
        // daylight at the origin.
        QcEntityMesh world;
        const QcEntityMesher::ItemLook held = QcFigureLook::item(content, pickaxe);
        QcEntityMesher::appendModel(world, {QcFigureLook::player(content, look), &held}, QcAnimator::animateBody(pose, 3.0),
                                    {{}, 155.0, QcEntityLight(1.0f, 0.0f), false}, anyUv);
        QCOMPARE(preview.indices, world.indices);
        QCOMPARE(preview.vertices.size(), world.vertices.size());
        for (qsizetype i = 0; i < world.vertices.size(); ++i) {
            QCOMPARE(preview.vertices[i].x, world.vertices[i].x);
            QCOMPARE(preview.vertices[i].y, world.vertices[i].y);
            QCOMPARE(preview.vertices[i].tint, world.vertices[i].tint);
        }
        const bool shirt = std::ranges::any_of(preview.vertices, [](const QcEntityVertex &v) { return v.tint == std::array<quint8, 4>{0x12, 0x34, 0x56, 255}; });
        QVERIFY(shirt);

        // An unknown outfit falls back to the default one, as in the world; no humanoid, no figure.
        look.outfit = u"mod:nothing"_s;
        QVERIFY(!QcPreviewScene::build(content, {look, qcNoItem, {}, 180.0, 0.0}, anyUv).indices.isEmpty());
        QVERIFY(QcPreviewScene::build(QcContentRegistry(), {look, qcNoItem, {}, 180.0, 0.0}, anyUv).indices.isEmpty());
    }

    void headYawTurnsOnlyTheHead()
    {
        QStringList warnings;
        const auto base = QcBaseContent::load(warnings);
        QVERIFY(base);
        const QcAppearance look;
        QcEntityPose straight;
        QcEntityPose turned;
        turned.headYaw = 60.0f;
        // Facing -Z (yaw 0): turning to the right moves the face, which sits in front of the head, to +X.
        const QcEntityMesh a = QcPreviewScene::build(**base, {look, qcNoItem, straight, 0.0, 0.0}, anyUv);
        const QcEntityMesh b = QcPreviewScene::build(**base, {look, qcNoItem, turned, 0.0, 0.0}, anyUv);
        QCOMPARE(a.vertices.size(), b.vertices.size());
        // The body below the neck stays where it was; the face moved to +X.
        QVERIFY(std::abs(meanX(a, 0.75, 1.3) - meanX(b, 0.75, 1.3)) < 1e-4);
        double faceShift = 0.0;
        for (qsizetype i = 0; i < a.vertices.size(); ++i) {
            if (a.vertices[i].y > 1.36 && a.vertices[i].z < -0.226f) {
                faceShift = std::max(faceShift, double(b.vertices[i].x - a.vertices[i].x));
            }
        }
        QVERIFY2(faceShift > 0.1, qPrintable(QString::number(faceShift)));
        // The animator keeps the head within 80 degrees.
        turned.headYaw = 170.0f;
        QCOMPARE(QcAnimator::animateBody(turned, 0.0).bone(QcBoneRole::Head).yaw, 80.0);
    }

    void hookParsesAndRejects()
    {
        const QcResult<QcPreviewHook> hook = QcPreviewHook::parse(u"animation=walk, side=back,zoom=0.5,gaze=0.8:-0.2,time=1.25,turn=-30"_s);
        QVERIFY2(hook, qPrintable(hook ? QString() : hook.error()));
        QCOMPARE(hook->animation, QcPreviewPose::Animation::Walk);
        QCOMPARE(hook->side, QcPreviewCamera::Side::Back);
        QCOMPARE(hook->zoom, 0.5);
        QCOMPARE(hook->gaze, std::pair(0.8, -0.2));
        QCOMPARE(hook->time, 1.25);
        QCOMPARE(hook->turn, -30.0);
        QVERIFY(QcPreviewHook::parse(QString())->isEmpty());

        for (const QString &bad : {u"animation=dance"_s, u"zoom=2"_s, u"gaze=1"_s, u"gaze=0:3"_s, u"colour=red"_s, u"walk"_s, u"time=-1"_s}) {
            const QcResult<QcPreviewHook> refused = QcPreviewHook::parse(bad);
            QVERIFY2(!refused, qPrintable(bad));
            QVERIFY(refused.error().contains(bad));
        }
    }
};

QTEST_GUILESS_MAIN(TestCharacterPreview)
#include "test_character_preview.moc"
