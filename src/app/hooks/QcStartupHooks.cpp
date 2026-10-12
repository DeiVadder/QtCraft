#include "QcStartupHooks.h"

#include "QcBenchmarkRun.h"
#include "QcClientSession.h"
#include "QcGameController.h"
#include "QcMeshScheduler.h"
#include "QcScreenshotHook.h"
#include "QcSessionParts.h"
#include "QcWorldView.h"

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>
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

void QcStartupHooks::menuShown()
{
    scheduleOverlaySteps();
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
    } else if (m_startupOverlay == u"players"_s) {
        // As if the player list key were held.
        m_game.input()->setPlayerListHeld(true);
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
            // Pointer and popup steps work in the main menu as well, the others need a world.
            if (m_parts.session || overlay.startsWith(u"open:"_s) || overlay.startsWith(u"hover:"_s)) {
                runOverlayStep(overlay);
            }
            releaseScreenshot();
        });
    }
}

void QcStartupHooks::runOverlayStep(const QString &step)
{
    if (step.startsWith(u"pick:"_s)) {
        pickInOpenScreen(QStringView(step).mid(5).toInt());
    } else if (step.startsWith(u"open:"_s)) {
        openPopup(step.mid(5));
    } else if (step.startsWith(u"hover:"_s)) {
        hoverAt(QStringView(step).mid(6));
    } else if (step == u"handbook"_s) {
        m_game.handbook()->open({});
    } else if (step == u"players"_s) {
        m_game.input()->setPlayerListHeld(true);
    } else {
        // "@<ms> " closes the screen and the handbook.
        m_game.setOverlay(step);
        if (step.isEmpty()) {
            m_game.handbook()->close();
        }
    }
}

void QcStartupHooks::pickInOpenScreen(int row) const
{
    if (!m_parts.window) {
        return;
    }
    const QList<QQuickItem *> screens = m_parts.window->findChildren<QQuickItem *>(u"slotScreen"_s);
    for (QQuickItem *screen : screens) {
        if (screen->isVisible()) {
            screen->setProperty("pickedSlot", row);
            return;
        }
    }
    qCWarning(lcGame) << "--overlay pick:" << row << ": no slot screen is open";
}

void QcStartupHooks::hoverAt(QStringView position) const
{
    const qsizetype comma = position.indexOf(u',');
    bool validX = false;
    bool validY = false;
    const QPointF point(position.left(comma).toDouble(&validX), position.mid(comma + 1).toDouble(&validY));
    // The window that renders the view: the offscreen window when the content moved there.
    QQuickWindow *window = m_game.view() ? m_game.view()->window() : nullptr;
    if (comma < 0 || !validX || !validY || !window) {
        qCWarning(lcGame) << "--overlay hover:" << position << ": expected 'hover:<x>,<y>' in window coordinates";
        return;
    }

    // A mouse move like the real pointer's: hover handlers, tooltips and the hovered item follow.
    QMouseEvent move(QEvent::MouseMove, point, window->mapToGlobal(point), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &move);
}

void QcStartupHooks::openPopup(const QString &objectName) const
{
    const QQuickWindow *window = m_game.view() ? m_game.view()->window() : nullptr;
    // An empty name would find the first unnamed object.
    QObject *control = window && !objectName.isEmpty() ? window->contentItem()->findChild<QObject *>(objectName) : nullptr;
    if (!control) {
        qCWarning(lcGame) << "--overlay open:" << objectName << ": no such control";
        return;
    }
    // A combo box opens its list; a popup or dialog opens itself.
    QObject *popup = control->property("popup").value<QObject *>();
    QMetaObject::invokeMethod(popup ? popup : control, "open");
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
    // The server streams the view distance (plus a ring) it granted in Welcome; a network server
    // may grant less than the settings ask for.
    const int streamed = m_parts.session->viewDistance() > 0 ? m_parts.session->viewDistance() : viewDistance + 1;
    const int ring = 2 * streamed + 1;
    const bool allChunksArrived = m_parts.session->world()->chunkCount() >= qsizetype(ring) * ring;
    if (m_screenshotHook && !m_screenshotReleased && playing && allChunksArrived && m_parts.meshes->dirtyCount() == 0
        && m_parts.meshes->jobsInFlight() == 0) {
        m_screenshotReleased = true;
        m_screenshotHook->release();
    }
}
