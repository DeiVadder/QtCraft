#include "QcRenderBridge.h"

#include "QcClientSession.h"
#include "QcFigureLook.h"
#include "QcLightVolume.h"
#include "QcMeshScheduler.h"
#include "QcPhysics.h"
#include "QcSessionParts.h"
#include "QcSettingsController.h"
#include "QcSky.h"
#include "QcTextureAtlas.h"

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

QcVec3 lerp(const QcVec3 &a, const QcVec3 &b, double t)
{
    return a + (b - a) * t;
}

double lerpYaw(float from, float to, double t)
{
    const float delta = std::fmod(to - from + 540.0f, 360.0f) - 180.0f;
    return double(from + delta * float(t));
}

// Glowing creatures pulse slowly, each in its own rhythm (`phase`); at full glow they are drawn
// brighter than any lit surface (the shader multiplies, so values above 1 shine).
QcEntityLight glowingLight(QcEntityLight light, const QcMobDefinition &mob, double phase, double seconds)
{
    constexpr double emission = 1.8;
    const double pulse = 0.75 + 0.25 * std::sin(seconds * 2.2 + phase);
    const float glow = float(emission * pulse * mob.glow / double(QcMobDefinition::maxGlow));
    return QcEntityLight(light.sky, std::max(light.block, glow));
}

} // namespace

QcRenderBridge::QcRenderBridge(const QcSessionParts &parts, const QcSettingsController &settings)
    : m_parts(parts)
    , m_settings(settings)
{
}

void QcRenderBridge::placeCamera(const QcVec3 &eye, QcCameraMode mode)
{
    const QcClientSession &session = *m_parts.session;
    const QcViewCamera::Placement placed = QcViewCamera::place(eye, session.intent().yaw, session.intent().pitch, mode, *session.world());
    QcCamera camera = m_parts.view->camera();
    camera.position = QVector3D(float(placed.position.x), float(placed.position.y), float(placed.position.z));
    camera.yaw = placed.yaw;
    camera.pitch = placed.pitch;
    camera.fieldOfView = float(m_settings.fieldOfView());
    m_parts.view->setCamera(camera);
}

float QcRenderBridge::blockLightAt(QcBlockPos pos) const
{
    return float(m_parts.meshes->blockLight(pos)) / float(QcLightVolume::maxLevel);
}

std::optional<QcEntityMesher::ItemLook> QcRenderBridge::heldLook(QcItemId item) const
{
    const auto content = m_parts.session->content();
    return content ? QcFigureLook::held(*content, item) : std::nullopt;
}

QcEntityMesher::ModelLook QcRenderBridge::playerLook(const QcAppearance &appearance) const
{
    return QcFigureLook::player(*m_parts.session->content(), appearance);
}

QcEntityPose QcRenderBridge::localPose(double alpha) const
{
    QcEntityPose pose = m_parts.session->localPose(alpha);
    if (m_forcedPose.isEmpty()) {
        return pose;
    }

    // Screenshot hook: a fixed moment of an animation, independent of the real movement.
    pose.onGround = true;
    if (m_forcedPose == u"walk"_s) {
        pose.speed = QcPhysics::walkSpeed;
        pose.walkDistance = 0.3;
    } else if (m_forcedPose.startsWith(u"swing"_s)) {
        // "swing:0.3" freezes that moment of the swing (screenshot series).
        bool ok = false;
        const double progress = m_forcedPose.section(u':', 1).toDouble(&ok);
        pose.swingProgress = ok ? std::clamp(progress, 0.0, 1.0) : 0.45;
    } else if (m_forcedPose == u"swim"_s) {
        pose.onGround = false;
        pose.inWater = true;
        pose.speed = QcPhysics::swimSpeed;
    } else if (m_forcedPose == u"jump"_s) {
        pose.onGround = false;
    } else if (m_forcedPose == u"sneak"_s) {
        pose.sneaking = true;
    } else if (m_forcedPose == u"death"_s) {
        pose.deathProgress = 0.7;
    }
    return pose;
}

float QcRenderBridge::skyExposure(const QcClientWorld &world, QcBlockPos pos)
{
    // Share of the 3 x 3 columns around `pos` open to the sky: light fades gradually under the
    // edge of a roof or a tree instead of switching.
    constexpr float covered = 0.35f;
    int open = 0;
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            const QcBlockPos column = pos.offset(dx, 0, dz);
            const std::shared_ptr<const QcChunk> chunk = world.chunk(column.chunk());
            open += !chunk || chunk->highestNonAir(column.localX(), column.localZ()) < pos.y ? 1 : 0;
        }
    }
    return covered + (1.0f - covered) * float(open) / 9.0f;
}

QcEntityLight QcRenderBridge::entityLight(const QcClientWorld &world, const QcVec3 &position) const
{
    const QcBlockPos feet{int(std::floor(position.x)), int(std::floor(position.y)), int(std::floor(position.z))};
    return QcEntityLight(skyExposure(world, feet.offset(0, 1, 0)), blockLightAt(feet.offset(0, 1, 0)));
}

void QcRenderBridge::appendCreature(QcEntityMesh &mesh, const QcClientEntity &entity, const EntityFrame &frame) const
{
    const QList<QcMobDefinition> &mobs = frame.content.mobs();
    const bool mob = entity.mobType >= 0 && entity.mobType < mobs.size();
    const QcVec3 position = lerp(entity.previousPosition, entity.position, frame.alpha);
    QcEntityMesher::Placement placement{position, lerpYaw(entity.previousYaw, entity.yaw, frame.alpha), entityLight(frame.world, position),
                                        entity.hurtTicks > 0};
    if (mob && mobs[entity.mobType].glow > 0) {
        placement.light = glowingLight(placement.light, mobs[entity.mobType], double(entity.id % 97), frame.seconds);
    }

    QcEntityPose pose = QcClientSession::poseOf(entity, frame.alpha);
    QcEntityMesher::ModelLook look;
    if (mob && !mobs[entity.mobType].skeleton.isEmpty()) {
        const QcMobDefinition &definition = mobs[entity.mobType];
        look.model = frame.content.model(definition.skeleton);
        look.outfit = frame.content.appearance(definition.outfit);
        look.scale = definition.height / QcEntityMesher::modelHeight;
        pose.armsForward = definition.armsForward;
    } else if (!mob) {
        look = playerLook(entity.appearance);
    }
    if (!look.isValid()) {
        // A skeleton or outfit that did not load: the mob's own boxes, else the plain player figure.
        const bool ownParts = mob && !mobs[entity.mobType].parts.isEmpty();
        QcEntityMesher::appendParts(mesh, ownParts ? mobs[entity.mobType].parts : QcEntityMesher::playerParts(), placement, frame.uv);
        return;
    }

    const std::optional<QcEntityMesher::ItemLook> held = heldLook(entity.held);
    QcEntityMesher::appendModel(mesh, {look, held ? &*held : nullptr}, QcAnimator::animateBody(pose, frame.seconds), placement, frame.uv);
}

void QcRenderBridge::appendLocalPlayer(QcEntityMesh &mesh, const EntityFrame &frame) const
{
    const QcClientSession &session = *m_parts.session;
    const QcVec3 feet = lerp(session.previousPlayerMotion().position, session.playerMotion().position, frame.alpha);
    const QcItemStack &selected = session.inventory().selectedStack();
    const std::optional<QcEntityMesher::ItemLook> held = heldLook(selected.isEmpty() ? qcNoItem : selected.item);
    QcEntityMesher::appendModel(mesh, {playerLook(m_settings.character()->appearance()), held ? &*held : nullptr},
                                QcAnimator::animateBody(localPose(frame.alpha), frame.seconds),
                                {feet, double(session.intent().yaw), entityLight(frame.world, feet), false}, frame.uv);
}

void QcRenderBridge::updateEntities(double alpha, QcCameraMode mode)
{
    if (!m_atlas) {
        return;
    }

    auto mesh = std::make_shared<QcEntityMesh>();
    // Sized like the previous frame's mesh, so the lists do not grow vertex by vertex.
    mesh->vertices.reserve(m_entityMeshSize.first);
    mesh->indices.reserve(m_entityMeshSize.second);
    const QcClientSession &session = *m_parts.session;
    const QcEntityMesher::UvLookup uv = [this](const QString &texture) { return m_atlas->uv(texture); };
    const EntityFrame frame{*session.content(), *session.world(), uv, alpha, session.worldTime() + alpha * QcPhysics::tickDuration};

    for (const QcClientEntity &entity : session.entities()) {
        if (entity.isItem()) {
            QcEntityMesher::appendItem(*mesh, entity, QcFigureLook::item(frame.content, entity.item.item),
                                       {alpha, frame.seconds, entityLight(frame.world, entity.position)}, uv);
        } else if (entity.isFallingBlock()) {
            QcEntityMesher::ItemLook look;
            look.cube = true;
            look.faces = frame.content.block(entity.block).faceTextures;
            const QcVec3 position = lerp(entity.previousPosition, entity.position, alpha);
            QcEntityMesher::appendBlock(*mesh, look, position, entityLight(frame.world, position), uv);
        } else {
            appendCreature(*mesh, entity, frame);
        }
    }
    for (const QcClientEntity &entity : session.dyingEntities()) {
        appendCreature(*mesh, entity, frame);
    }
    // In third person the player sees their own character.
    if (mode != QcCameraMode::FirstPerson && !session.isDead()) {
        appendLocalPlayer(*mesh, frame);
    }

    m_entityMeshSize = {mesh->vertices.size(), mesh->indices.size()};
    m_parts.view->setEntityMesh(std::move(mesh));
}

void QcRenderBridge::updateHand(double alpha, const QcVec3 &eye, QcCameraMode mode)
{
    if (mode == QcCameraMode::FirstPerson) {
        updateHandMesh(alpha, eye);
    } else {
        m_parts.view->setHandMesh(nullptr);
    }
}

void QcRenderBridge::updateHandMesh(double alpha, const QcVec3 &eye)
{
    const QcClientSession &session = *m_parts.session;
    if (!m_atlas || session.isDead()) {
        m_parts.view->setHandMesh(nullptr);
        return;
    }

    auto mesh = std::make_shared<QcEntityMesh>();
    const QcEntityMesher::UvLookup uv = [this](const QString &texture) { return m_atlas->uv(texture); };
    const QcItemStack &selected = session.inventory().selectedStack();
    const std::optional<QcEntityMesher::ItemLook> held = heldLook(selected.isEmpty() ? qcNoItem : selected.item);
    const QcClientSession::HandTimers timers = session.handTimers(alpha);
    const double seconds = session.worldTime() + alpha * QcPhysics::tickDuration;
    const QcAnimator::HandPose hand = QcAnimator::animateHand(localPose(alpha), seconds, timers.switchProgress, timers.landing, timers.use);
    // The arm takes the light around the eyes: sky light by how open the sky is, torches nearby.
    const QcBlockPos head{int(std::floor(eye.x)), int(std::floor(eye.y)), int(std::floor(eye.z))};
    const QcEntityLight light(skyExposure(*session.world(), head), blockLightAt(head));
    QcEntityMesher::appendHand(*mesh, {playerLook(m_settings.character()->appearance()), held ? &*held : nullptr}, hand,
                               {eye, session.intent().yaw, session.intent().pitch, light}, uv);
    m_parts.view->setHandMesh(std::move(mesh));
}

void QcRenderBridge::updateSky(double alpha)
{
    m_parts.view->setSky(QcSky::at(m_parts.session->worldTime() + alpha * QcPhysics::tickDuration));
}

void QcRenderBridge::updateSelection()
{
    const QcClientSession &session = *m_parts.session;
    const std::optional<QcRaycastHit> &target = session.target();
    QList<QcAabb> selection;
    if (target && !session.targetEntity()) {
        const std::optional<QcBlockId> block = session.world()->blockAt(target->block);
        const QcVec3 corner{double(target->block.x), double(target->block.y), double(target->block.z)};
        if (block && session.content()->isValidBlock(*block)) {
            const QList<QcAabb> boxes = session.content()->state(*block).targetBoxes();
            for (const QcAabb &box : boxes) {
                selection.append(box.moved(corner));
            }
        }
    }
    m_parts.view->setSelection(selection);
}
