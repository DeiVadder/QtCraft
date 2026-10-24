#include "app/session/QcFrameDriver.h"

#include "app/controllers/QcHudState.h"
#include "app/controllers/QcSettingsController.h"
#include "app/display/QcFrameLimiter.h"
#include "app/hooks/QcBenchmarkRun.h"
#include "app/hooks/QcStartupHooks.h"
#include "app/input/QcGameInput.h"
#include "app/preview/QcPreviewSource.h"
#include "app/rendering/QcRenderBridge.h"
#include "app/session/QcSessionParts.h"
#include "clientcore/debug/QcClientHealth.h"
#include "clientcore/debug/QcDebugJson.h"
#include "clientcore/debug/QcStatsLog.h"
#include "clientcore/input/QcGamepadControls.h"
#include "clientcore/meshing/QcMeshScheduler.h"
#include "clientcore/session/QcClientSession.h"
#include "common/diagnostics/QcHealthLog.h"
#include "common/physics/QcPhysics.h"

#include <algorithm>

namespace {

QcVec3 lerp(const QcVec3 &a, const QcVec3 &b, double t)
{
    return a + (b - a) * t;
}

} // namespace

QcFrameDriver::QcFrameDriver(QcSessionParts &parts, const Targets &targets, std::function<bool()> playing)
    : m_parts(parts)
    , m_targets(targets)
    , m_playing(std::move(playing))
{
    m_frameClock.start();
}

QcFrameDriver::~QcFrameDriver() = default;

void QcFrameDriver::startTickClock()
{
    m_sinceTick.start();
}

void QcFrameDriver::restartTickClock()
{
    m_sinceTick.restart();
}

float QcFrameDriver::advanceFrameClocks()
{
    const qint64 sinceLast = m_frameClock.restart();
    if (m_healthLog) {
        m_frameTimes.add(static_cast<double>(sinceLast));
    }
    return static_cast<float>(std::min<qint64>(sinceLast, 100)) / 1000.0f;
}

void QcFrameDriver::startHealthLog(int seconds)
{
    if (seconds <= 0 || m_healthLog) {
        return;
    }

    m_healthLog = std::make_unique<QcHealthLog>(seconds, [this] {
        QcHealthSample sample = m_parts.session ? QcClientHealth::sample(*m_parts.session) : QcHealthSample{};
        sample.frames = m_frameTimes.take();
        return sample;
    });
}

void QcFrameDriver::startStatsLog(int intervalMs)
{
    if (intervalMs <= 0 || m_statsLog) {
        return;
    }

    m_targets.hud.debug()->setStatsLogging(true);
    m_statsLog = std::make_unique<QcStatsLog>(intervalMs, [this]() -> std::optional<QJsonObject> {
        if (!m_parts.session || m_parts.session->state() != QcClientSession::State::Playing) {
            return std::nullopt;
        }
        return QcDebugJson::toJson(m_targets.hud.debug()->snapshot(m_drawnFeet, QcFrameMonitor::Reader::StatsLog));
    });
}

void QcFrameDriver::onFrame()
{
    // A frame before the frame rate limit allows (an item changed in the previous one asked for
    // it) leaves the scene as it is, so nothing new is drawn; the limiter asks for the next one
    // when it is due (decision E-835).
    QcFrameLimiter &limiter = m_targets.settings.display()->frames();
    if (m_parts.session && !limiter.frameDue(m_parts.window)) {
        return;
    }
    ++m_framesHandled;
    m_targets.benchmark.recordFrame();
    const float frameSeconds = advanceFrameClocks();
    const QPointF mouseDelta = m_targets.input.takeMouseDelta();
    // Taken every frame, also in the menu, so it never adds up across frames.
    const qint64 previewNs = m_targets.preview.takeBuildNs();
    if (!m_parts.session || !m_parts.view || !m_parts.session->world()) {
        return;
    }

    // The world moves every frame (interpolated motion, animations, day cycle): the next frame is
    // requested explicitly, not left to item changes made during this one, when the limit allows.
    limiter.requestNextFrame(m_parts.window);
    m_targets.input.applyFrame(mouseDelta, frameSeconds);
    // After the keyboard: a deflected stick wins over the held keys, a centred one leaves them.
    m_targets.gamepad.applyFrame(*m_parts.session, frameSeconds, m_targets.input.worldFocused());

    const double alpha = std::clamp(static_cast<double>(m_sinceTick.elapsed()) / (QcPhysics::tickDuration * 1000.0), 0.0, 1.0);
    const QcVec3 feet = lerp(m_parts.session->prediction().previousMotion().position, m_parts.session->playerMotion().position, alpha);
    const QcVec3 eye = feet + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
    // The control interface may switch the view too; the session holds the current mode.
    const QcCameraMode mode = m_parts.session->isDead() ? QcCameraMode::FirstPerson : m_parts.session->cameraMode();
    m_targets.render.placeCamera(eye, mode, frameSeconds, alpha);

    QElapsedTimer entityTime;
    entityTime.start();
    m_targets.render.updateEntities(alpha, mode);
    m_targets.render.updateHand(alpha, eye, mode);
    const qint64 entityNs = entityTime.nsecsElapsed();
    const QList<QcRenderSample> renderSamples = m_targets.hud.debug()->recordFrame(entityNs, previewNs, m_targets.benchmark.isRunning());
    m_targets.benchmark.recordGuiFrame(m_targets.hud.debug()->lastGuiCpuMs(), entityNs, previewNs, renderSamples);
    m_targets.render.updateSky(alpha);
    m_targets.render.updateWeather(eye, frameSeconds);
    m_targets.hud.updateTimeOfDay(alpha);
    m_targets.hud.updateDial(alpha);
    m_targets.hud.heldMap()->update();
    m_targets.render.updateSelection();
    m_targets.hud.updateTarget();
    m_targets.hud.updateLabels();

    m_parts.meshes->setFocus({feet.x, feet.y, feet.z});
    m_parts.meshes->pump(*m_parts.session->world());

    m_drawnFeet = feet;
    m_targets.hud.debug()->update(feet);
    m_targets.hud.debug()->updateWorld(alpha, feet);
    m_targets.startup.releaseScreenshotWhenLoaded(m_targets.settings.viewDistance(), m_playing());
}
