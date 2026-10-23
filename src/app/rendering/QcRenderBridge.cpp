#include "app/rendering/QcRenderBridge.h"

#include "app/controllers/QcSettingsController.h"
#include "app/session/QcSessionParts.h"
#include "clientcore/light/QcSky.h"
#include "clientcore/session/QcClientSession.h"
#include "common/physics/QcPhysics.h"

#include <algorithm>

namespace {

// The widened view stays below a fish-eye even from the widest setting (110°).
constexpr double maxWidenedFieldOfView = 130.0;

} // namespace

QcRenderBridge::QcRenderBridge(const QcSessionParts &parts, const QcSettingsController &settings)
    : m_parts(parts)
    , m_settings(settings)
    , m_entities(parts, settings)
{
}

void QcRenderBridge::placeCamera(const QcVec3 &eye, QcCameraMode mode, double frameSeconds)
{
    // Sprinting widens the view only while the player is faster than walking: not at a wall, not
    // swimming (where a sprint does nothing).
    const QcEntityPose pose = m_entities.localPose(1.0);
    const bool sprinting = pose.sprinting && !pose.inWater && pose.speed > QcPhysics::walkSpeed + 0.5;
    m_fov.setShare(m_settings.fovEffects());
    const double widening = m_fov.advance({.sprinting = sprinting, .flying = pose.flying}, frameSeconds);

    const QcClientSession &session = *m_parts.session;
    const QcViewCamera::Placement placed = QcViewCamera::place(eye, session.intent().yaw, session.intent().pitch, mode, *session.world());
    QcCamera camera = m_parts.view->camera();
    camera.position = QVector3D(static_cast<float>(placed.position.x), static_cast<float>(placed.position.y), static_cast<float>(placed.position.z));
    camera.yaw = placed.yaw;
    camera.pitch = placed.pitch;
    camera.fieldOfView = static_cast<float>(std::min(m_settings.fieldOfView() * widening, maxWidenedFieldOfView));
    m_parts.view->setCamera(camera);
}

void QcRenderBridge::updateEntities(double alpha, QcCameraMode mode)
{
    // Without an atlas the view keeps the last entity mesh it was given.
    if (std::shared_ptr<QcEntityMesh> mesh = m_entities.buildEntities(alpha, mode)) {
        m_parts.view->setEntityMesh(std::move(mesh));
        m_parts.view->setFramedMaps(m_entities.framedMaps(), m_entities.framedMapTiles());
    }
}

void QcRenderBridge::updateHand(double alpha, const QcVec3 &eye, QcCameraMode mode)
{
    // A spectator has no hand (decision E-730).
    const bool shown = mode == QcCameraMode::FirstPerson && !m_handHidden && m_parts.session->gameMode().rules().tangible;
    m_parts.view->setHandMesh(shown ? m_entities.buildHand(alpha, eye) : nullptr);
}

void QcRenderBridge::updateSky(double alpha)
{
    const QcClientSession &session = *m_parts.session;
    const QcClientWeather &weather = session.worldMirror().weather();
    QcSkyState sky = QcSky::at(session.worldMirror().worldTime() + alpha * QcPhysics::tickDuration);
    sky = QcSky::withWeather(sky, weather.rain(), weather.thunder(), weather.flash(static_cast<double>(session.tickCount()) + alpha));
    m_parts.view->setSky(QcSky::seenFrom(sky, m_parts.view->camera().position.y()));
}

void QcRenderBridge::updateWeather(const QcVec3 &eye, double frameSeconds)
{
    const QcClientSession &session = *m_parts.session;
    if (!session.world()) {
        return;
    }

    m_precipitation.update(*session.world(), session.worldMirror().weather(), eye, frameSeconds);
    if (m_precipitation.particles().isEmpty()) {
        m_parts.view->setPrecipitation(nullptr);
        return;
    }

    m_parts.view->setPrecipitation(m_drops.fill(m_precipitation, session.worldMirror().weather().rain()));
}

void QcRenderBridge::updateSelection()
{
    const QcClientSession &session = *m_parts.session;
    const std::optional<QcRaycastHit> &target = session.hand().target();
    QList<QcAabb> selection;
    if (target && !session.hand().targetEntity() && !m_outlineHidden) {
        const std::optional<QcBlockId> block = session.world()->blockAt(target->block);
        const QcVec3 corner{static_cast<double>(target->block.x), static_cast<double>(target->block.y), static_cast<double>(target->block.z)};
        if (block && session.content()->isValidBlock(*block)) {
            const QList<QcAabb> boxes = session.content()->state(*block).targetBoxes();
            for (const QcAabb &box : boxes) {
                selection.append(box.moved(corner));
            }
        }
    }
    m_parts.view->setSelection(selection);
}
