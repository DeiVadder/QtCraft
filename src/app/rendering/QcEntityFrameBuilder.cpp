#include "app/rendering/QcEntityFrameBuilder.h"

#include "app/controllers/QcSettingsController.h"
#include "app/session/QcSessionParts.h"
#include "clientcore/meshing/QcFigureLook.h"
#include "clientcore/meshing/QcHangingMesher.h"
#include "clientcore/meshing/QcMeshScheduler.h"
#include "clientcore/meshing/QcProjectileMesher.h"
#include "clientcore/session/QcClientSession.h"
#include "clientcore/textures/QcTextureAtlas.h"
#include "common/world/QcLightVolume.h"

#include <algorithm>
#include <array>
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

QcEntityFrameBuilder::QcEntityFrameBuilder(const QcSessionParts &parts, const QcSettingsController &settings)
    : m_parts(parts)
    , m_settings(settings)
{
}

float QcEntityFrameBuilder::blockLightAt(QcBlockPos pos) const
{
    return float(m_parts.meshes->blockLight(pos)) / float(QcLightVolume::maxLevel);
}

std::optional<QcEntityMesher::ItemLook> QcEntityFrameBuilder::heldLook(QcItemId item) const
{
    const auto content = m_parts.session->content();
    return content ? QcFigureLook::held(*content, item) : std::nullopt;
}

QcEntityMesher::ModelLook QcEntityFrameBuilder::playerLook(const QcAppearance &appearance) const
{
    return QcFigureLook::player(*m_parts.session->content(), appearance);
}

QcEntityPose QcEntityFrameBuilder::localPose(double alpha) const
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

float QcEntityFrameBuilder::skyExposure(const QcClientWorld &world, QcBlockPos pos)
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

QcEntityLight QcEntityFrameBuilder::entityLight(const QcClientWorld &world, const QcVec3 &position) const
{
    const QcBlockPos feet{int(std::floor(position.x)), int(std::floor(position.y)), int(std::floor(position.z))};
    return QcEntityLight(skyExposure(world, feet.offset(0, 1, 0)), blockLightAt(feet.offset(0, 1, 0)));
}

QcCapeDrive QcEntityFrameBuilder::localCapeDrive(const QcEntityPose &pose) const
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

double QcEntityFrameBuilder::capeFrameSeconds()
{
    const double seconds = m_capeClock.isValid() ? double(m_capeClock.restart()) / 1000.0 : 0.0;
    if (!m_capeClock.isValid()) {
        m_capeClock.start();
    }
    return std::min(seconds, QcCapeSwing::maxAdvance);
}

void QcEntityFrameBuilder::appendCreature(QcEntityMesh &mesh, const QcClientEntity &entity, const EntityFrame &frame, double scale)
{
    const QcMobDefinition *creature = entity.creature(frame.content);
    const QcVec3 position = lerp(entity.previousPosition, entity.position, frame.alpha);
    QcEntityMesher::Placement placement{position, lerpYaw(entity.previousYaw, entity.yaw, frame.alpha), entityLight(frame.world, position),
                                        entity.hurtTicks > 0};
    placement.scale = scale;
    if ((entity.flags & QcMsg::Young) != 0) {
        placement.scale *= QcMsg::youngScale;
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

    QcEntityPose pose = QcLocalAnimation::poseOf(entity, frame.alpha);
    QcEntityMesher::ModelLook look;
    if (creature && creature->villager) {
        // Villagers wear the look the server gave them (decision E-411).
        look = playerLook(entity.appearance);
        look.scale = creature->height / QcEntityMesher::modelHeight * placement.scale;
    } else if (creature && !creature->skeleton.isEmpty()) {
        const QcMobDefinition &definition = *creature;
        look.model = frame.content.assets().model(definition.skeleton);
        look.outfit = frame.content.assets().appearance(definition.outfit);
        look.scale = definition.height / QcEntityMesher::modelHeight * placement.scale;
        look.cape = QcFigureLook::cape(frame.content, definition.cape);
        pose.armsForward = definition.armsForward;
    } else if (!creature) {
        look = playerLook(entity.appearance);
    }
    if (!look.isValid()) {
        // A skeleton or outfit that did not load: the mob's own boxes, else the plain player figure.
        const bool ownParts = creature && !creature->parts.isEmpty();
        const QcEntityMesher::PartLook parts{QcMsg::poseOf(entity.flags) == QcPose::Sitting, entity.appearance};
        QcEntityMesher::appendParts(mesh, ownParts ? creature->parts : QcEntityMesher::playerParts(), placement, frame.uv, parts);
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
    const QcFigureLook::Dressed dressed = QcFigureLook::armour(frame.content, entity.equipment, std::move(look));
    QcEntityMesher::appendModel(mesh, {dressed.look, held ? &*held : nullptr, &dressed.armour}, body, placement, frame.uv);
}

void QcEntityFrameBuilder::appendBlockEntity(QcEntityMesh &mesh, const QcClientEntity &entity, const EntityFrame &frame)
{
    QcEntityMesher::ItemLook look;
    look.cube = true;
    look.faces = frame.content.block(entity.block).faceTextures;
    const QcVec3 position = lerp(entity.previousPosition, entity.position, frame.alpha);
    QcEntityMesher::BlockPlacement placement{position};
    QcEntityLight light = entityLight(frame.world, position);
    // A lit explosive flashes white every few ticks and swells in its last ticks (decision E-604).
    if (entity.isPrimedExplosive()) {
        const int fuse = int(entity.health);
        if ((fuse / primedFlashTicks) % 2 == 0) {
            light = QcEntityLight(1.0f, 1.0f);
        }
        if (fuse < primedSwellTicks) {
            placement.size *= 1.0 + 0.2 * (1.0 - double(fuse) / primedSwellTicks);
        }
    }
    QcEntityMesher::appendBlock(mesh, look, placement, light, frame.uv);
}

void QcEntityFrameBuilder::appendExplosions(QcEntityMesh &mesh, const EntityFrame &frame, double ticks)
{
    QcEntityMesher::ItemLook flash;
    flash.cube = true;
    flash.faces.fill(QString::fromLatin1(explosionFlashTexture));
    QcEntityMesher::ItemLook smoke;
    smoke.cube = true;
    smoke.faces.fill(QString::fromLatin1(explosionSmokeTexture));

    for (const QcExplosionEffects::Puff &puff : m_parts.session->worldMirror().extras().explosions().puffs()) {
        if (const std::optional<QcExplosionEffects::Look> look = QcExplosionEffects::lookAt(puff, ticks)) {
            const QcEntityLight light = look->flash ? QcEntityLight(1.0f, 1.0f) : entityLight(frame.world, look->position);
            QcEntityMesher::appendBlock(mesh, look->flash ? flash : smoke, {look->position, look->size}, light, frame.uv);
        }
    }
}

void QcEntityFrameBuilder::appendSpawners(QcEntityMesh &mesh, const EntityFrame &frame)
{
    // Only spawners this near are drawn: the creature is small, and a world may hold many.
    constexpr double drawDistance = 48.0;
    // The creature fits into this much of the cage and turns this many degrees a second.
    constexpr double fit = 0.55;
    constexpr double turnSpeed = 120.0;

    const QcClientSession &session = *m_parts.session;
    const QcVec3 viewer = session.playerMotion().position;
    const QHash<QcBlockPos, int> &spawners = session.worldMirror().extras().spawners();
    for (auto it = spawners.cbegin(); it != spawners.cend(); ++it) {
        const QcBlockPos pos = it.key();
        const QcVec3 feet{pos.x + 0.5, pos.y + 0.18, pos.z + 0.5};
        if (it.value() < 0 || it.value() >= frame.content.mobs().size() || (feet - viewer).length() > drawDistance) {
            continue;
        }
        const QcMobDefinition &kind = frame.content.mobs()[it.value()];
        QcClientEntity shown;
        // An id no entity of the server has (the capes follow entities by id).
        shown.id = 0xff000000u | (quint32(qHash(pos)) & 0x00ffffffu);
        shown.kind = QcEntityKind::Creature;
        shown.creatureType = it.value();
        shown.position = feet;
        shown.previousPosition = feet;
        shown.yaw = float(std::fmod(frame.seconds * turnSpeed + (pos.x * 37 + pos.z * 11) % 360, 360.0));
        shown.previousYaw = shown.yaw;
        appendCreature(mesh, shown, frame, std::min(1.0, fit / std::max({kind.height, kind.width, 0.1})));
    }
}

void QcEntityFrameBuilder::appendSigns(QcEntityMesh &mesh, const EntityFrame &frame)
{
    // Text farther away is too small to read; a world may hold many signs.
    constexpr double drawDistance = 32.0;

    const QcChunkExtras &extras = m_parts.session->worldMirror().extras();
    m_signTexts.setSigns(extras.signs(), extras.signGeneration());
    const QList<QcBlockPos> near = m_signTexts.signsNear(m_parts.session->playerMotion().position, drawDistance);
    for (const QcBlockPos pos : near) {
        const std::optional<QcBlockId> block = frame.world.blockAt(pos);
        if (!block) {
            continue;
        }
        // The text lies inside the sign's cell, so it has the light of the cell's middle.
        const QcEntityLight light = entityLight(frame.world, {pos.x + 0.5, pos.y + 0.5, pos.z + 0.5});
        for (const QcSignTextMesher::Quad &quad : m_signTexts.quads(pos, *block, light, frame.content, frame.uv)) {
            mesh.addQuad(quad);
        }
    }
}

void QcEntityFrameBuilder::appendCampfires(QcEntityMesh &mesh, const EntityFrame &frame)
{
    // Items lie on the logs, one on each side, turned towards the middle.
    constexpr double drawDistance = 48.0;
    constexpr double top = 7.0 / 16.0;
    constexpr double offset = 0.22;
    static constexpr std::array<std::pair<double, double>, 4> places{{{-offset, -offset}, {offset, -offset}, {offset, offset}, {-offset, offset}}};

    const QcClientSession &session = *m_parts.session;
    const QcVec3 viewer = session.playerMotion().position;
    const QHash<QcBlockPos, QList<quint32>> &campfires = session.worldMirror().extras().campfires();
    for (auto it = campfires.cbegin(); it != campfires.cend(); ++it) {
        const QcBlockPos pos = it.key();
        const QcVec3 centre{pos.x + 0.5, pos.y + top, pos.z + 0.5};
        if ((centre - viewer).length() > drawDistance) {
            continue;
        }
        for (qsizetype slot = 0; slot < std::min<qsizetype>(it->size(), qsizetype(places.size())); ++slot) {
            const QcItemId item = it->at(slot);
            if (!frame.content.isValidItem(item)) {
                continue;
            }
            const auto [dx, dz] = places[std::size_t(slot)];
            const QcEntityMesher::LyingPlacement placement{centre + QcVec3{dx, 0.0, dz}, 45.0 + 90.0 * double(slot), entityLight(frame.world, centre)};
            QcEntityMesher::appendLyingItem(mesh, QcFigureLook::item(frame.content, item), placement, frame.uv);
        }
    }

    QcEntityMesher::ItemLook smoke;
    smoke.cube = true;
    smoke.faces.fill(QString::fromLatin1(explosionSmokeTexture));
    m_smoke.refresh(frame.world, viewer, session.tickCount());
    for (const QcBlockPos &campfire : m_smoke.campfires()) {
        const QList<QcCampfireSmoke::Puff> puffs = QcCampfireSmoke::puffsOf(campfire, frame.seconds);
        for (const QcCampfireSmoke::Puff &puff : puffs) {
            QcEntityMesher::appendBlock(mesh, smoke, {puff.position, puff.size}, entityLight(frame.world, puff.position), frame.uv);
        }
    }
}

void QcEntityFrameBuilder::appendStand(QcEntityMesh &mesh, const QcClientEntity &entity, const QcEntityMesher::Placement &placement,
                                 const EntityFrame &frame) const
{
    QcEntityMesher::ModelLook look = QcFigureLook::stand(frame.content, entity.stand);
    if (!look.isValid()) {
        return;
    }

    look.cape = QcFigureLook::cape(frame.content, entity.appearance.cape);
    const std::optional<QcEntityMesher::ItemLook> held = entity.stand.arms ? heldLook(entity.held) : std::nullopt;
    const QcFigureLook::Dressed dressed = QcFigureLook::standArmour(frame.content, entity.equipment, std::move(look));
    QcEntityMesher::appendModel(mesh, {dressed.look, held ? &*held : nullptr, &dressed.armour, QcFigureLook::standHiddenBones(entity.stand)},
                                QcFigureLook::standBody(entity.stand, frame.seconds), placement, frame.uv);
}

void QcEntityFrameBuilder::appendCartLoad(QcEntityMesh &mesh, const QcClientEntity &entity, const EntityFrame &frame, const QcVec3 &position) const
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

void QcEntityFrameBuilder::appendLocalPlayer(QcEntityMesh &mesh, const EntityFrame &frame)
{
    const QcClientSession &session = *m_parts.session;
    const QcVec3 feet = lerp(session.prediction().previousMotion().position, session.playerMotion().position, frame.alpha);
    const QcItemStack &selected = session.inventory().selectedStack();
    const std::optional<QcEntityMesher::ItemLook> held = heldLook(selected.isEmpty() ? qcNoItem : selected.item);
    const QcFigureLook::Dressed dressed = QcFigureLook::armour(frame.content, session.inventoryMirror().equipment(), playerLook(m_settings.character()->appearance()));
    const QcEntityPose pose = localPose(frame.alpha);

    QcAnimator::BodyPose body = QcAnimator::animateBody(pose, frame.seconds);
    if (dressed.look.cape) {
        const QcCapeDrive drive = localCapeDrive(pose);
        QcCapeSwing::Angles cape = m_capes.advance(QcCapeTracker::localPlayer, drive);
        if (!m_forcedPose.isEmpty()) {
            // Screenshots show the cape at rest on its target for the forced movement.
            cape = QcCapeSwing::target(drive, frame.seconds);
        }
        body.capeLift = cape.lift;
        body.capeSway = cape.sway;
    }
    QcEntityMesher::appendModel(mesh, {dressed.look, held ? &*held : nullptr, &dressed.armour}, body,
                                {feet, double(session.intent().yaw), entityLight(frame.world, feet), false}, frame.uv);
    if (session.vitals().burning()) {
        appendFlames(mesh, frame, feet, {QcPhysics::playerWidth, QcPhysics::playerHeight});
    }
}

void QcEntityFrameBuilder::appendFlames(QcEntityMesh &mesh, const EntityFrame &frame, const QcVec3 &feet, QcPhysics::BodySize body)
{
    if (const std::optional<QcEntityMesher::ItemLook> look = QcFigureLook::flames(frame.content, body.width, body.height)) {
        QcEntityMesher::appendBlock(mesh, *look, {feet, body.height}, 1.0f, frame.uv);
    }
}

std::shared_ptr<QcEntityMesh> QcEntityFrameBuilder::buildEntities(double alpha, QcCameraMode mode)
{
    if (!m_atlas) {
        return nullptr;
    }

    auto mesh = std::make_shared<QcEntityMesh>();
    // Sized like the previous frame's mesh, so the lists do not grow vertex by vertex.
    mesh->vertices.reserve(m_entityMeshSize.first);
    mesh->indices.reserve(m_entityMeshSize.second);
    const QcClientSession &session = *m_parts.session;
    const QcEntityMesher::UvLookup uv = [this](const QString &texture) { return m_atlas->uv(texture); };
    const EntityFrame frame{*session.content(), *session.world(), uv, alpha, session.worldMirror().worldTime() + alpha * QcPhysics::tickDuration};
    m_capes.beginFrame(capeFrameSeconds(), frame.seconds);

    for (const QcClientEntity &entity : session.entities()) {
        if (entity.isItem()) {
            QcEntityMesher::appendItem(*mesh, entity, QcFigureLook::item(frame.content, entity.item.item),
                                       {alpha, frame.seconds, entityLight(frame.world, entity.position)}, uv);
        } else if (entity.isExperienceOrb()) {
            QcEntityMesher::appendOrb(*mesh, entity, {alpha, frame.seconds, entityLight(frame.world, entity.position)}, uv);
        } else if (entity.isProjectile()) {
            const QcVec3 centre = lerp(entity.previousPosition, entity.position, alpha);
            const QcProjectileMesher::Placement placement{centre, lerpYaw(entity.previousYaw, entity.yaw, alpha),
                                                          entity.previousPitch + (entity.pitch - entity.previousPitch) * alpha,
                                                          entityLight(frame.world, centre)};
            QcProjectileMesher::append(*mesh, placement, uv(QcFigureLook::item(frame.content, entity.item.item).sprite));
        } else if (entity.isMinecart()) {
            const QcVec3 position = lerp(entity.previousPosition, entity.position, alpha);
            QcEntityMesher::appendParts(*mesh, QcEntityMesher::minecartParts(),
                                        {position, lerpYaw(entity.previousYaw, entity.yaw, alpha), entityLight(frame.world, position), false}, uv);
            appendCartLoad(*mesh, entity, frame, position);
        } else if (entity.isBlockEntity()) {
            appendBlockEntity(*mesh, entity, frame);
        } else if (entity.isHanging()) {
            QcHangingMesher::append(*mesh, entity, frame.content, entityLight(frame.world, entity.position), uv);
        } else {
            appendCreature(*mesh, entity, frame);
        }
    }
    for (const QcClientEntity &entity : session.entityMirror().dying()) {
        appendCreature(*mesh, entity, frame);
    }
    appendSpawners(*mesh, frame);
    appendSigns(*mesh, frame);
    appendCampfires(*mesh, frame);
    appendExplosions(*mesh, frame, double(session.tickCount()) + alpha);
    // In third person the player sees their own character; a spectator has none (decision E-730).
    if (mode != QcCameraMode::FirstPerson && !session.isDead() && session.gameMode().rules().tangible) {
        appendLocalPlayer(*mesh, frame);
    }
    m_capes.endFrame();

    m_entityMeshSize = {mesh->vertices.size(), mesh->indices.size()};
    return mesh;
}

std::shared_ptr<QcEntityMesh> QcEntityFrameBuilder::buildHand(double alpha, const QcVec3 &eye) const
{
    const QcClientSession &session = *m_parts.session;
    if (!m_atlas || session.isDead()) {
        return nullptr;
    }

    auto mesh = std::make_shared<QcEntityMesh>();
    const QcEntityMesher::UvLookup uv = [this](const QString &texture) { return m_atlas->uv(texture); };
    const QcItemStack &selected = session.inventory().selectedStack();
    const std::optional<QcEntityMesher::ItemLook> held = heldLook(selected.isEmpty() ? qcNoItem : selected.item);
    const QcHandTimers timers = session.handTimers(alpha);
    const double seconds = session.worldMirror().worldTime() + alpha * QcPhysics::tickDuration;
    const QcAnimator::HandPose hand = QcAnimator::animateHand(localPose(alpha), seconds, timers.switchProgress, timers.landing, timers.use);
    // The arm takes the light around the eyes: sky light by how open the sky is, torches nearby.
    const QcBlockPos head{int(std::floor(eye.x)), int(std::floor(eye.y)), int(std::floor(eye.z))};
    const QcEntityLight light(skyExposure(*session.world(), head), blockLightAt(head));
    QcEntityMesher::appendHand(*mesh, {playerLook(m_settings.character()->appearance()), held ? &*held : nullptr}, hand,
                               {eye, session.intent().yaw, session.intent().pitch, light}, uv);
    return mesh;
}
