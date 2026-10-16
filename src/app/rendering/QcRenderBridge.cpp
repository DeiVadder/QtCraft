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

// The widened view stays below a fish-eye even from the widest setting (110°).
constexpr double maxWidenedFieldOfView = 130.0;

} // namespace

QcRenderBridge::QcRenderBridge(const QcSessionParts &parts, const QcSettingsController &settings)
    : m_parts(parts)
    , m_settings(settings)
{
}

void QcRenderBridge::placeCamera(const QcVec3 &eye, QcCameraMode mode, double frameSeconds)
{
    // Sprinting widens the view only while the player is faster than walking: not at a wall, not
    // swimming (where a sprint does nothing).
    const QcEntityPose pose = localPose(1.0);
    const bool sprinting = pose.sprinting && !pose.inWater && pose.speed > QcPhysics::walkSpeed + 0.5;
    m_fov.setShare(m_settings.fovEffects());
    const double widening = m_fov.advance({.sprinting = sprinting, .flying = pose.flying}, frameSeconds);

    const QcClientSession &session = *m_parts.session;
    const QcViewCamera::Placement placed = QcViewCamera::place(eye, session.intent().yaw, session.intent().pitch, mode, *session.world());
    QcCamera camera = m_parts.view->camera();
    camera.position = QVector3D(float(placed.position.x), float(placed.position.y), float(placed.position.z));
    camera.yaw = placed.yaw;
    camera.pitch = placed.pitch;
    camera.fieldOfView = float(std::min(m_settings.fieldOfView() * widening, maxWidenedFieldOfView));
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
    } else if (m_forcedPose == u"sprint"_s) {
        pose.speed = QcPhysics::sprintSpeed;
        pose.sprinting = true;
        pose.walkDistance = 0.3;
    } else if (m_forcedPose.startsWith(u"swing"_s)) {
        // "swing:0.3" freezes that moment of the swing (screenshot series).
        bool ok = false;
        const double progress = m_forcedPose.section(u':', 1).toDouble(&ok);
        pose.swingProgress = ok ? std::clamp(progress, 0.0, 1.0) : 0.45;
    } else if (m_forcedPose == u"fly"_s || m_forcedPose == u"flysprint"_s) {
        // Creative flight (decision E-284), with sprint the widest view.
        pose.onGround = false;
        pose.flying = true;
        pose.sprinting = m_forcedPose == u"flysprint"_s;
        pose.speed = pose.sprinting ? QcPhysics::flySprintSpeed : QcPhysics::flySpeed;
    } else if (m_forcedPose == u"swim"_s) {
        pose.onGround = false;
        pose.inWater = true;
        pose.speed = QcPhysics::swimSpeed;
    } else if (m_forcedPose == u"jump"_s || m_forcedPose == u"fall"_s) {
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

QcCapeDrive QcRenderBridge::localCapeDrive(const QcEntityPose &pose) const
{
    const QcClientSession &session = *m_parts.session;
    const double yaw = double(session.intent().yaw);
    if (m_forcedPose.isEmpty()) {
        return QcCapeDrive::from(session.playerMotion().velocity, yaw, pose);
    }

    // The forced pose's movement: forwards at its speed, rising after a jump, falling fast.
    QcCapeDrive drive = QcCapeDrive::from({}, yaw, pose);
    drive.forwardSpeed = pose.speed;
    if (m_forcedPose == u"jump"_s) {
        drive.verticalSpeed = QcPhysics::jumpVelocity / 2.0;
    } else if (m_forcedPose == u"fall"_s) {
        drive.verticalSpeed = -QcPhysics::jumpVelocity * 2.0;
    }
    return drive;
}

double QcRenderBridge::capeFrameSeconds()
{
    const double seconds = m_capeClock.isValid() ? double(m_capeClock.restart()) / 1000.0 : 0.0;
    if (!m_capeClock.isValid()) {
        m_capeClock.start();
    }
    return std::min(seconds, QcCapeSwing::maxAdvance);
}

void QcRenderBridge::appendCreature(QcEntityMesh &mesh, const QcClientEntity &entity, const EntityFrame &frame)
{
    const QcMobDefinition *creature = entity.creature(frame.content);
    const QcVec3 position = lerp(entity.previousPosition, entity.position, frame.alpha);
    QcEntityMesher::Placement placement{position, lerpYaw(entity.previousYaw, entity.yaw, frame.alpha), entityLight(frame.world, position),
                                        entity.hurtTicks > 0};
    if ((entity.flags & QcMsg::Young) != 0) {
        placement.scale = QcMsg::youngScale;
    }
    if (creature && creature->glow > 0) {
        placement.light = glowingLight(placement.light, *creature, double(entity.id % 97), frame.seconds);
    }
    if ((entity.flags & QcMsg::Burning) != 0) {
        const QcPhysics::BodySize body = creature ? QcPhysics::BodySize{creature->width, creature->height}
                                                  : QcPhysics::BodySize{QcPhysics::playerWidth, QcPhysics::playerHeight};
        appendFlames(mesh, frame, position, {body.width * placement.scale, body.height * placement.scale});
    }

    if (entity.isArmourStand()) {
        appendStand(mesh, entity, placement, frame);
        return;
    }

    QcEntityPose pose = QcClientSession::poseOf(entity, frame.alpha);
    QcEntityMesher::ModelLook look;
    if (creature && creature->villager) {
        // Villagers wear the look the server gave them (decision E-411).
        look = playerLook(entity.appearance);
        look.scale = creature->height / QcEntityMesher::modelHeight * placement.scale;
    } else if (creature && !creature->skeleton.isEmpty()) {
        const QcMobDefinition &definition = *creature;
        look.model = frame.content.model(definition.skeleton);
        look.outfit = frame.content.appearance(definition.outfit);
        look.scale = definition.height / QcEntityMesher::modelHeight * placement.scale;
        look.cape = QcFigureLook::cape(frame.content, definition.cape);
        pose.armsForward = definition.armsForward;
    } else if (!creature) {
        look = playerLook(entity.appearance);
    }
    if (!look.isValid()) {
        // A skeleton or outfit that did not load: the mob's own boxes, else the plain player figure.
        const bool ownParts = creature && !creature->parts.isEmpty();
        QcEntityMesher::appendParts(mesh, ownParts ? creature->parts : QcEntityMesher::playerParts(), placement, frame.uv);
        return;
    }

    QcAnimator::BodyPose body = QcAnimator::animateBody(pose, frame.seconds);
    if (look.cape) {
        // The last tick's move gives the velocity; between ticks the entity glides on.
        const QcVec3 velocity = (entity.position - entity.previousPosition) * QcPhysics::tickRate;
        const QcCapeSwing::Angles cape = m_capes.advance(entity.id, QcCapeDrive::from(velocity, placement.yaw, pose));
        body.capeLift = cape.lift;
        body.capeSway = cape.sway;
    }
    const std::optional<QcEntityMesher::ItemLook> held = heldLook(entity.held);
    const QList<QcEntityMesher::Attachment> armour = QcFigureLook::armour(frame.content, entity.equipment, look);
    QcEntityMesher::appendModel(mesh, {look, held ? &*held : nullptr, &armour}, body, placement, frame.uv);
}

void QcRenderBridge::appendStand(QcEntityMesh &mesh, const QcClientEntity &entity, const QcEntityMesher::Placement &placement,
                                 const EntityFrame &frame) const
{
    QcEntityMesher::ModelLook look = QcFigureLook::stand(frame.content, entity.stand);
    if (!look.isValid()) {
        return;
    }

    look.cape = QcFigureLook::cape(frame.content, entity.appearance.cape);
    const std::optional<QcEntityMesher::ItemLook> held = entity.stand.arms ? heldLook(entity.held) : std::nullopt;
    const QList<QcEntityMesher::Attachment> armour = QcFigureLook::standArmour(frame.content, entity.equipment, look);
    QcEntityMesher::appendModel(mesh, {look, held ? &*held : nullptr, &armour, QcFigureLook::standHiddenBones(entity.stand)},
                                QcFigureLook::standBody(entity.stand, frame.seconds), placement, frame.uv);
}

void QcRenderBridge::appendCartLoad(QcEntityMesh &mesh, const QcClientEntity &entity, const EntityFrame &frame, const QcVec3 &position) const
{
    const std::optional<QcBlockId> block = entity.cartBlock(frame.content);
    const std::optional<QcItemId> item = block ? frame.content.itemId(frame.content.block(*block).name) : std::nullopt;
    if (!item) {
        return;
    }
    // A block of 0.7 m standing on the floor of the cart.
    constexpr double size = 0.7;
    const QcVec3 floor = position + QcVec3{0.0, 0.18, 0.0};
    QcEntityMesher::appendBlock(mesh, QcFigureLook::item(frame.content, *item), {floor, size}, entityLight(frame.world, floor), frame.uv);
}

void QcRenderBridge::appendLocalPlayer(QcEntityMesh &mesh, const EntityFrame &frame)
{
    const QcClientSession &session = *m_parts.session;
    const QcVec3 feet = lerp(session.previousPlayerMotion().position, session.playerMotion().position, frame.alpha);
    const QcItemStack &selected = session.inventory().selectedStack();
    const std::optional<QcEntityMesher::ItemLook> held = heldLook(selected.isEmpty() ? qcNoItem : selected.item);
    QcEntityMesher::ModelLook look = playerLook(m_settings.character()->appearance());
    const QList<QcEntityMesher::Attachment> armour = QcFigureLook::armour(frame.content, session.equipment(), look);
    const QcEntityPose pose = localPose(frame.alpha);

    QcAnimator::BodyPose body = QcAnimator::animateBody(pose, frame.seconds);
    if (look.cape) {
        const QcCapeDrive drive = localCapeDrive(pose);
        QcCapeSwing::Angles cape = m_capes.advance(QcCapeTracker::localPlayer, drive);
        if (!m_forcedPose.isEmpty()) {
            // Screenshots show the cape at rest on its target for the forced movement.
            cape = QcCapeSwing::target(drive, frame.seconds);
        }
        body.capeLift = cape.lift;
        body.capeSway = cape.sway;
    }
    QcEntityMesher::appendModel(mesh, {look, held ? &*held : nullptr, &armour}, body,
                                {feet, double(session.intent().yaw), entityLight(frame.world, feet), false}, frame.uv);
    if (session.vitals().burning()) {
        appendFlames(mesh, frame, feet, {QcPhysics::playerWidth, QcPhysics::playerHeight});
    }
}

void QcRenderBridge::appendFlames(QcEntityMesh &mesh, const EntityFrame &frame, const QcVec3 &feet, QcPhysics::BodySize body)
{
    if (const std::optional<QcEntityMesher::ItemLook> look = QcFigureLook::flames(frame.content, body.width, body.height)) {
        QcEntityMesher::appendBlock(mesh, *look, {feet, body.height}, 1.0f, frame.uv);
    }
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
    m_capes.beginFrame(capeFrameSeconds(), frame.seconds);

    for (const QcClientEntity &entity : session.entities()) {
        if (entity.isItem()) {
            QcEntityMesher::appendItem(*mesh, entity, QcFigureLook::item(frame.content, entity.item.item),
                                       {alpha, frame.seconds, entityLight(frame.world, entity.position)}, uv);
        } else if (entity.isExperienceOrb()) {
            QcEntityMesher::appendOrb(*mesh, entity, {alpha, frame.seconds, entityLight(frame.world, entity.position)}, uv);
        } else if (entity.isMinecart()) {
            const QcVec3 position = lerp(entity.previousPosition, entity.position, alpha);
            QcEntityMesher::appendParts(*mesh, QcEntityMesher::minecartParts(),
                                        {position, lerpYaw(entity.previousYaw, entity.yaw, alpha), entityLight(frame.world, position), false}, uv);
            appendCartLoad(*mesh, entity, frame, position);
        } else if (entity.isBlockEntity()) {
            QcEntityMesher::ItemLook look;
            look.cube = true;
            look.faces = frame.content.block(entity.block).faceTextures;
            const QcVec3 position = lerp(entity.previousPosition, entity.position, alpha);
            QcEntityMesher::appendBlock(*mesh, look, {position}, entityLight(frame.world, position), uv);
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
    m_capes.endFrame();

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
    const QcHandTimers timers = session.handTimers(alpha);
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
    const QcClientSession &session = *m_parts.session;
    const QcClientWeather &weather = session.weather();
    QcSkyState sky = QcSky::at(session.worldTime() + alpha * QcPhysics::tickDuration);
    sky = QcSky::withWeather(sky, weather.rain(), weather.thunder(), weather.flash(double(session.tickCount()) + alpha));
    m_parts.view->setSky(QcSky::seenFrom(sky, m_parts.view->camera().position.y()));
}

void QcRenderBridge::updateWeather(const QcVec3 &eye, double frameSeconds)
{
    const QcClientSession &session = *m_parts.session;
    if (!session.world()) {
        return;
    }

    m_precipitation.update(*session.world(), session.weather(), eye, frameSeconds);
    if (m_precipitation.particles().isEmpty()) {
        m_parts.view->setPrecipitation(nullptr);
        return;
    }

    // A list neither the view nor the renderer holds any more: they copy the pointer only while the
    // GUI thread waits in the renderer's synchronize(), so nobody can take it while it is filled.
    const auto slot = std::ranges::find_if(m_dropLists, [](const auto &list) { return !list || list.use_count() == 1; });
    std::shared_ptr<QList<QcPrecipitationDrop>> drops = slot != m_dropLists.end() ? *slot : nullptr;
    if (!drops) {
        drops = std::make_shared<QList<QcPrecipitationDrop>>();
        if (slot != m_dropLists.end()) {
            *slot = drops;
        }
    }
    m_precipitation.fillDrops(session.weather().rain(), *drops);
    m_parts.view->setPrecipitation(std::move(drops));
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
