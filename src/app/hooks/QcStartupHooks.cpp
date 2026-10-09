#include "QcStartupHooks.h"

#include "QcBenchmarkRun.h"
#include "QcClientSession.h"
#include "QcGameController.h"
#include "QcMeshScheduler.h"
#include "QcScreenshotHook.h"
#include "QcSessionParts.h"

#include <QLoggingCategory>
#include <QTimer>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

Q_LOGGING_CATEGORY(lcGame, "qtcraft.game")

// The delay of a line "@<ms> <rest>"; std::nullopt when the line has none.
std::optional<int> delayOf(const QString &line, qsizetype space)
{
    bool valid = false;
    const int delayMs = space > 1 ? QStringView(line).mid(1, space - 1).toInt(&valid) : 0;
    if (!valid || delayMs < 0) {
        return std::nullopt;
    }
    return delayMs;
}

} // namespace

QcStartupHooks::QcStartupHooks(QcGameController &game, const QcSessionParts &parts, QcBenchmarkRun *benchmark, QObject *parent)
    : QObject(parent)
    , m_game(game)
    , m_parts(parts)
    , m_benchmark(benchmark)
{
}

void QcStartupHooks::setInitialLook(float yaw, float pitch)
{
    m_initialYaw = yaw;
    m_initialPitch = std::clamp(pitch, -90.0f, 90.0f);
}

QcPlayerInput QcStartupHooks::initialIntent() const
{
    QcPlayerInput intent;
    intent.yaw = m_initialYaw;
    intent.pitch = m_initialPitch;
    return intent;
}

void QcStartupHooks::setStartupScript(QStringList chatLines, const QStringList &overlays)
{
    m_startupChat = std::move(chatLines);
    m_startupOverlay.clear();
    m_startupOverlaySteps.clear();
    for (const QString &overlay : overlays) {
        if (overlay.startsWith(u'@')) {
            m_startupOverlaySteps.append(overlay);
        } else if (m_startupOverlay.isEmpty()) {
            m_startupOverlay = overlay;
        }
    }
}

void QcStartupHooks::setScreenshotHook(QcScreenshotHook *hook)
{
    m_screenshotHook = hook;
    m_screenshotReleased = false;
    if (hook) {
        hook->hold();
    }
}

void QcStartupHooks::holdScreenshot() const
{
    if (m_screenshotHook) {
        m_screenshotHook->hold();
    }
}

void QcStartupHooks::releaseScreenshot() const
{
    if (m_screenshotHook) {
        m_screenshotHook->release();
    }
}

void QcStartupHooks::worldPlaying()
{
    sendStartupChat();
    openStartupOverlay();
    scheduleOverlaySteps();
    m_benchmark->scheduleStart();
}

void QcStartupHooks::sendStartupChat()
{
    for (const QString &line : std::as_const(m_startupChat)) {
        // "@<ms> <text>" waits that long after loading (scenes in motion for screenshots);
        // the screenshot is taken only after the last delayed line.
        const qsizetype space = line.indexOf(u' ');
        const std::optional<int> delayMs = line.startsWith(u'@') ? delayOf(line, space) : std::nullopt;
        if (!delayMs) {
            m_parts.session->sendChat(line);
            continue;
        }

        holdScreenshot();
        QTimer::singleShot(*delayMs, this, [this, text = line.mid(space + 1)] {
            if (m_parts.session) {
                m_parts.session->sendChat(text);
            }
            releaseScreenshot();
        });
    }
}

void QcStartupHooks::openStartupOverlay()
{
    if (m_startupOverlay == u"furnace"_s || m_startupOverlay == u"chest"_s) {
        openMachineInView();
    } else if (m_startupOverlay == u"handbook"_s || !m_startupHandbook.isEmpty()) {
        m_game.handbook()->open(m_startupHandbook);
    } else if (!m_startupOverlay.isEmpty()) {
        m_game.setOverlay(m_startupOverlay);
    }
}

void QcStartupHooks::openMachineInView()
{
    m_fillMachineOnOpen = true;
    holdScreenshot();
    QTimer::singleShot(1000, this, [this] {
        if (m_parts.session) {
            m_parts.session->useItem();
        }
    });
    // Without a machine in view the screen never opens; take the screenshot anyway.
    QTimer::singleShot(6000, this, [this] {
        if (!m_fillMachineOnOpen) {
            return;
        }
        m_fillMachineOnOpen = false;
        qCWarning(lcGame) << "--overlay" << m_startupOverlay << ": no block in view opened";
        releaseScreenshot();
    });
}

void QcStartupHooks::scheduleOverlaySteps()
{
    for (const QString &step : std::as_const(m_startupOverlaySteps)) {
        const qsizetype space = step.indexOf(u' ');
        const std::optional<int> delayMs = delayOf(step, space);
        if (!delayMs) {
            qCWarning(lcGame) << "--overlay" << step << ": expected '@<ms> <overlay>'";
            continue;
        }

        holdScreenshot();
        QTimer::singleShot(*delayMs, this, [this, overlay = step.mid(space + 1)] {
            if (m_parts.session) {
                m_game.setOverlay(overlay);
            }
            releaseScreenshot();
        });
    }
}

void QcStartupHooks::machineOpened()
{
    if (!m_fillMachineOnOpen) {
        return;
    }

    m_fillMachineOnOpen = false;
    QcClientSession &session = *m_parts.session;
    if (session.containerOpen()) {
        for (int slot = 0; slot < 9; ++slot) {
            session.moveInventoryItem(slot, QcMsg::MoveInventoryItem::machineInputSlot + slot);
        }
        QTimer::singleShot(1000, this, [this] { releaseScreenshot(); });
        return;
    }

    // Hotbar slot 1 goes into the input, slot 2 onto the fuel.
    session.moveInventoryItem(0, QcMsg::MoveInventoryItem::machineInputSlot);
    session.moveInventoryItem(1, QcMsg::MoveInventoryItem::machineFuelSlot);
    // A few seconds of burning show the flame and the arrow in the screenshot.
    QTimer::singleShot(2500, this, [this] { releaseScreenshot(); });
}

void QcStartupHooks::releaseScreenshotWhenLoaded(int viewDistance, bool playing)
{
    const int ring = 2 * (viewDistance + 1) + 1;
    const bool allChunksArrived = m_parts.session->world()->chunkCount() >= qsizetype(ring) * ring;
    if (m_screenshotHook && !m_screenshotReleased && playing && allChunksArrived && m_parts.meshes->dirtyCount() == 0
        && m_parts.meshes->jobsInFlight() == 0) {
        m_screenshotReleased = true;
        m_screenshotHook->release();
    }
}
