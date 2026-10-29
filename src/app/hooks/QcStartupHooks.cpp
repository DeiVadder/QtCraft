// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/hooks/QcStartupHooks.h"

#include "app/controllers/QcHandbookController.h"
#include "app/controllers/QcScreenState.h"
#include "app/debug/QcAppLogging.h"
#include "app/hooks/QcBenchmarkRun.h"
#include "app/hooks/QcScreenshotHook.h"
#include "app/hooks/QcScriptedPad.h"
#include "app/input/QcGameInput.h"
#include "app/rendering/QcWorldView.h"
#include "app/session/QcSessionParts.h"
#include "clientcore/meshing/QcMeshScheduler.h"
#include "clientcore/session/QcClientSession.h"
#include "common/core/QcBlockPos.h"
#include "common/core/QcWorldConstants.h"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// The items named `objectName` in the item tree below `item`: delegates of a Repeater hang in it
// by their parent item only, so QObject::findChildren() misses them.
[[nodiscard]] QList<QObject *> namedItems(const QQuickItem *item, const QString &objectName)
{
    QList<QObject *> found;
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *child : children) {
        if (child->objectName() == objectName) {
            found.append(child);
        }
        found.append(namedItems(child, objectName));
    }
    return found;
}

// The delay of a line "@<ms> <rest>"; std::nullopt when the line has none.
[[nodiscard]] std::optional<int> delayOf(const QString &line, qsizetype space)
{
    bool valid = false;
    const int delayMs = space > 1 ? QStringView(line).mid(1, space - 1).toInt(&valid) : 0;
    if (!valid || delayMs < 0) {
        return std::nullopt;
    }
    return delayMs;
}

// How many of the chunk coordinates from centre - radius to centre + radius lie within the bound of
// the world (QcWorld::maxHorizontalChunk).
[[nodiscard]] qsizetype chunksInsideWorld(int centre, int radius)
{
    const qint64 low = std::max<qint64>(qint64{centre} - radius, -QcWorld::maxHorizontalChunk);
    const qint64 high = std::min<qint64>(qint64{centre} + radius, QcWorld::maxHorizontalChunk);
    return static_cast<qsizetype>(std::max<qint64>(0, high - low + 1));
}

// While a screenshot waits for the world, how often it logs what it waits for.
constexpr qint64 screenshotWaitLogMs = 10000;

} // namespace

QcStartupHooks::QcStartupHooks(const Screens &screens, const QcSessionParts &parts, QcBenchmarkRun *benchmark, QObject *parent)
    : QObject(parent)
    , m_screens(screens)
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
        } else if (overlay.startsWith(u"message:"_s)) {
            // A step at once, so it shows in the main menu as well as in a world.
            m_startupOverlaySteps.append(u"@0 "_s + overlay);
        } else if (m_startupOverlay.isEmpty()) {
            m_startupOverlay = overlay;
        }
    }
}

void QcStartupHooks::setScriptedPad(QcScriptedPad *pad)
{
    m_scriptedPad = pad;
    if (pad) {
        connect(pad, &QcScriptedPad::stepReported, this, [this] {
            if (screenshotHook()) {
                screenshotHook()->checkPadStep();
            }
            releaseScreenshot();
        });
    }
}

void QcStartupHooks::setScreenshotHook(QcScreenshotHook *hook)
{
    m_screenshotHook = hook;
    m_screenshotReleased = false;
    m_screenshotWait.invalidate();
    if (hook) {
        hook->hold();
    }
}

void QcStartupHooks::setMenuScreenshotHook(QcScreenshotHook *hook)
{
    setScreenshotHook(hook);
    m_menuHold = hook != nullptr;
}

QcScreenshotHook *QcStartupHooks::screenshotHook() const
{
    return m_screenshotHook;
}

void QcStartupHooks::holdScreenshot() const
{
    if (screenshotHook()) {
        screenshotHook()->hold();
    }
}

void QcStartupHooks::releaseScreenshot() const
{
    if (screenshotHook()) {
        screenshotHook()->release();
    }
}

void QcStartupHooks::menuShown()
{
    // Each step holds the screenshot until it ran; then the menu's own hold ends.
    scheduleOverlaySteps();
    if (m_menuHold) {
        m_menuHold = false;
        m_screenshotReleased = true;
        releaseScreenshot();
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
    if (m_startupOverlay == u"furnace"_s || m_startupOverlay == u"chest"_s || m_startupOverlay == u"trade"_s
        || m_startupOverlay == u"enchanting"_s || m_startupOverlay == u"anvil"_s) {
        openMachineInView();
    } else if (m_startupOverlay == u"handbook"_s || !m_startupHandbook.isEmpty()) {
        m_screens.handbook.open(m_startupHandbook);
    } else if (m_startupOverlay == u"players"_s) {
        // As if the player list key were held.
        m_screens.input.setPlayerListHeld(true);
    } else if (!m_startupOverlay.isEmpty()) {
        openOverlay(m_startupOverlay);
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
            // Pointer, popup, text and pad steps work in the main menu as well, the others need a world.
            if (m_parts.session || overlay.startsWith(u"open:"_s) || overlay.startsWith(u"click:"_s) || overlay.startsWith(u"hover:"_s)
                || overlay.startsWith(u"pad:"_s) || overlay.startsWith(u"focus:"_s) || overlay.startsWith(u"text:"_s)
                || overlay.startsWith(u"message:"_s)) {
                runOverlayStep(overlay);
            }
            releaseScreenshot();
        });
    }
}

void QcStartupHooks::runOverlayStep(const QString &step)
{
    if (step.startsWith(u"pad:"_s)) {
        // Held until the step is reported (QcScriptedPad::stepReported).
        if (m_scriptedPad && m_scriptedPad->run(QStringView(step).mid(4))) {
            holdScreenshot();
        }
    } else if (step.startsWith(u"pick:"_s) || step.startsWith(u"open:"_s) || step.startsWith(u"text:"_s) || step.startsWith(u"click:"_s)
               || step.startsWith(u"focus:"_s)) {
        runOnLoadedScreen(step, 0);
    } else if (step.startsWith(u"hover:"_s)) {
        hoverAt(QStringView(step).mid(6));
    } else if (step.startsWith(u"message:"_s)) {
        // The message screen as after a world that could not start, with its way back.
        m_screens.state.setPhase(QcScreenState::Phase::Error, step.mid(8));
    } else if (step == u"handbook"_s) {
        m_screens.handbook.open({});
    } else if (step == u"players"_s) {
        m_screens.input.setPlayerListHeld(true);
    } else if (step.startsWith(u"key:"_s)) {
        pressKey(step.mid(4));
    } else if (step.startsWith(u"press:"_s)) {
        pressWindowKey(step.mid(6));
    } else if (step.startsWith(u"hold:"_s) || step.startsWith(u"release:"_s)) {
        holdKey(step.section(u':', 1), step.startsWith(u"hold:"_s));
    } else if (step == u"view:hide"_s || step == u"view:show"_s) {
        showView(step == u"view:show"_s, viewAttempts);
    } else if (step == u"fly"_s) {
        // Creative flight as after a double tap of jump (decision E-284).
        m_parts.session->gameMode().setFlightWanted(true);
    } else {
        // "@<ms> " closes the screen and the handbook.
        openOverlay(step);
        if (step.isEmpty()) {
            m_screens.handbook.close();
        }
    }
}

void QcStartupHooks::pressKey(const QString &actionId) const
{
    const std::optional<QcKeyBindings::Action> action = QcKeyBindings::actionFromId(actionId);
    if (!action) {
        qCWarning(lcGame) << "--overlay key:" << actionId << ": no such key action";
        return;
    }
    // The use key once, on what the player aims at (getting into a boat for a screenshot); the
    // world actions do not cover it.
    if (*action == QcKeyBindings::Action::Use) {
        m_parts.session->useItem();
        return;
    }
    // As if its key were pressed in the world.
    m_screens.input.worldAction(*action, true);
}

void QcStartupHooks::pressWindowKey(const QString &name) const
{
    const QKeySequence sequence = QKeySequence::fromString(name);
    // The world reads its keys at the game's window, the screens get them where their items are
    // (offscreen runs move them into another window).
    QQuickWindow *window = m_screens.input.worldInput() ? m_parts.window.data() : (m_parts.view ? m_parts.view->window() : nullptr);
    if (sequence.isEmpty() || !window) {
        qCWarning(lcGame) << "--overlay press:" << name << ": no such key or no window";
        return;
    }

    const QKeyCombination key = sequence[0];
    // A letter types itself into a text field, as on the keyboard.
    const QString text = name.size() == 1 ? name.toLower() : QString();
    QKeyEvent press(QEvent::KeyPress, key.key(), key.keyboardModifiers(), text);
    QKeyEvent release(QEvent::KeyRelease, key.key(), key.keyboardModifiers(), text);
    QCoreApplication::sendEvent(window, &press);
    QCoreApplication::sendEvent(window, &release);
    const QString overlay = QcScreenState::overlayName(m_screens.state.overlay());
    qCInfo(lcGame).noquote() << u"--overlay press:%1: overlay %2"_s.arg(name, overlay.isEmpty() ? u"none"_s : overlay);
}

void QcStartupHooks::holdKey(const QString &actionId, bool held) const
{
    const std::optional<QcKeyBindings::Action> action = QcKeyBindings::actionFromId(actionId);
    if (!action) {
        qCWarning(lcGame) << "--overlay hold:/release:" << actionId << ": no such key action";
        return;
    }
    // Held like a pressed key until a later release: step (paddling a boat for a screenshot).
    m_screens.input.holdAction(*action, held);
}

void QcStartupHooks::showView(bool shown, int attemptsLeft)
{
    if (!m_parts.view) {
        qCWarning(lcGame) << "--overlay view: no world view";
        return;
    }

    // A hide is only meaningful with meshed sections, a show only after the hide it follows.
    const bool ready = shown ? !m_parts.view->isVisible() : m_parts.view->meshCount() > 0;
    if (!ready && attemptsLeft > 0) {
        // The screenshot waits for the step, as for every other step.
        holdScreenshot();
        QTimer::singleShot(viewRetryMs, this, [this, shown, attemptsLeft] {
            showView(shown, attemptsLeft - 1);
            releaseScreenshot();
        });
        return;
    }

    // What the phase binding in Main.qml does when it hides the view (the binding is replaced).
    m_parts.view->setVisible(shown);
    qCInfo(lcGame).nospace() << (shown ? "World view shown (" : "World view hidden (") << m_parts.view->meshCount() << " sections)";
}

void QcStartupHooks::openOverlay(const QString &name) const
{
    const std::optional<QcScreenState::Overlay> overlay = QcScreenState::overlayFromName(name);
    if (!overlay) {
        qCWarning(lcGame) << "--overlay" << name << ": no such screen";
        return;
    }
    m_screens.state.setOverlay(*overlay);
}

void QcStartupHooks::runOnLoadedScreen(const QString &step, int attempt)
{
    bool done = false;
    if (step.startsWith(u"pick:"_s)) {
        done = pickInOpenScreen(QStringView(step).mid(5).toInt());
    } else if (step.startsWith(u"open:"_s)) {
        done = openPopup(step.mid(5));
    } else if (step.startsWith(u"click:"_s)) {
        done = clickControl(step.mid(6));
    } else if (step.startsWith(u"focus:"_s)) {
        done = focusControl(step.mid(6));
    } else {
        done = setText(step.mid(5));
    }
    if (done) {
        if (attempt > 0) {
            qCInfo(lcGame) << "--overlay" << step << ": waited" << attempt * loadedScreenRetryMs << "ms for its screen";
        }
        return;
    }

    // Screens are made when they open (LazyScreen, decision E-691): until the one the step needs
    // is there, the step and the screenshot wait for it.
    if (attempt + 1 >= loadedScreenAttempts) {
        qCWarning(lcGame) << "--overlay" << step << ": no such control on a shown screen";
        return;
    }
    holdScreenshot();
    QTimer::singleShot(loadedScreenRetryMs, this, [this, step, attempt] {
        runOnLoadedScreen(step, attempt + 1);
        releaseScreenshot();
    });
}

bool QcStartupHooks::pickInOpenScreen(int row) const
{
    if (!m_parts.window) {
        return false;
    }
    const QList<QQuickItem *> screens = m_parts.window->findChildren<QQuickItem *>(u"slotScreen"_s);
    for (QQuickItem *screen : screens) {
        if (screen->isVisible()) {
            screen->setProperty("pickedSlot", row);
            return true;
        }
    }
    return false;
}

void QcStartupHooks::hoverAt(QStringView position) const
{
    const qsizetype comma = position.indexOf(u',');
    bool validX = false;
    bool validY = false;
    const QPointF point(position.left(comma).toDouble(&validX), position.mid(comma + 1).toDouble(&validY));
    // The window that renders the view: the offscreen window when the content moved there.
    QQuickWindow *window = m_parts.view ? m_parts.view->window() : nullptr;
    if (comma < 0 || !validX || !validY || !window) {
        qCWarning(lcGame) << "--overlay hover:" << position << ": expected 'hover:<x>,<y>' in window coordinates";
        return;
    }

    // A mouse move like the real pointer's: hover handlers, tooltips and the hovered item follow.
    QMouseEvent move(QEvent::MouseMove, point, window->mapToGlobal(point), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &move);
}

bool QcStartupHooks::setText(const QString &assignment) const
{
    const qsizetype equals = assignment.indexOf(u'=');
    QObject *control = equals > 0 ? shownControl(assignment.left(equals), false) : nullptr;
    if (!control) {
        return false;
    }
    control->setProperty("text", assignment.mid(equals + 1));
    return true;
}

bool QcStartupHooks::clickControl(const QString &objectName) const
{
    QObject *control = shownControl(objectName, false);
    return control && QMetaObject::invokeMethod(control, "click");
}

bool QcStartupHooks::focusControl(const QString &objectName) const
{
    auto *control = qobject_cast<QQuickItem *>(shownControl(objectName, false));
    if (!control) {
        return false;
    }
    control->forceActiveFocus(Qt::TabFocusReason);
    return true;
}

bool QcStartupHooks::openPopup(const QString &objectName) const
{
    QObject *control = shownControl(objectName, true);
    if (!control) {
        return false;
    }
    // A combo box opens its list; a popup or dialog opens itself.
    QObject *popup = control->property("popup").value<QObject *>();
    QMetaObject::invokeMethod(popup ? popup : control, "open");
    return true;
}

QObject *QcStartupHooks::shownControl(const QString &objectName, bool parentShown) const
{
    const QQuickWindow *window = m_parts.view ? m_parts.view->window() : nullptr;
    // An empty name would find the first unnamed object.
    if (!window || objectName.isEmpty()) {
        return nullptr;
    }

    QList<QObject *> controls = window->contentItem()->findChildren<QObject *>(objectName);
    controls.append(namedItems(window->contentItem(), objectName));
    for (QObject *control : std::as_const(controls)) {
        auto *item = qobject_cast<QQuickItem *>(control);
        if (!parentShown) {
            if (item && item->isVisible()) {
                return control;
            }
            continue;
        }

        // A popup is no item; its "parent" property is the item it opens over.
        const QQuickItem *parent = item ? item->parentItem() : control->property("parent").value<QQuickItem *>();
        if (parent && parent->isVisible()) {
            return control;
        }
    }
    return nullptr;
}

void QcStartupHooks::tradeOpened()
{
    if (!m_fillMachineOnOpen) {
        return;
    }
    m_fillMachineOnOpen = false;
    QTimer::singleShot(1000, this, [this] { releaseScreenshot(); });
}

void QcStartupHooks::machineOpened()
{
    if (!m_fillMachineOnOpen) {
        return;
    }

    m_fillMachineOnOpen = false;
    const QcClientSession &session = *m_parts.session;
    if (session.machine().isContainer()) {
        for (int slot = 0; slot < 9; ++slot) {
            session.inventoryMirror().moveItem(slot, QcMsg::MoveInventoryItem::machineInputSlot + slot);
        }
        QTimer::singleShot(1000, this, [this] { releaseScreenshot(); });
        return;
    }

    // Hotbar slot 1 goes into the input, slot 2 onto the fuel.
    session.inventoryMirror().moveItem(0, QcMsg::MoveInventoryItem::machineInputSlot);
    session.inventoryMirror().moveItem(1, QcMsg::MoveInventoryItem::machineFuelSlot);
    // A few seconds of burning show the flame and the arrow in the screenshot.
    QTimer::singleShot(2500, this, [this] { releaseScreenshot(); });
}

void QcStartupHooks::releaseScreenshotWhenLoaded(int viewDistance, bool playing)
{
    if (!screenshotHook() || m_screenshotReleased || !playing) {
        return;
    }

    // The server streams the view distance (plus a ring) it granted in Welcome; a network server
    // may grant less than the settings ask for.
    const int streamed = m_parts.session->welcome().viewDistance > 0 ? m_parts.session->welcome().viewDistance : viewDistance + 1;
    // Near the bound of the world (decision E-883) the server sends only the chunks inside it.
    const QcVec3 &feet = m_parts.session->playerMotion().position;
    const QcChunkPos centre = QcBlockPos{static_cast<int>(std::floor(feet.x)), 0, static_cast<int>(std::floor(feet.z))}.chunk();
    const qsizetype expected = chunksInsideWorld(centre.x, streamed) * chunksInsideWorld(centre.z, streamed);
    const qsizetype arrived = m_parts.session->world()->chunkCount();
    const qsizetype dirty = m_parts.meshes->dirtyCount();
    const qsizetype jobs = m_parts.meshes->jobsInFlight();
    if (!m_screenshotWait.isValid()) {
        m_screenshotWait.start();
        m_screenshotWaitLogged = 0;
    }
    if (arrived >= expected && dirty == 0 && jobs == 0) {
        m_screenshotReleased = true;
        qCInfo(lcScreenshot) << "World ready for the screenshot after" << m_screenshotWait.elapsed() << "ms;" << screenshotHook()->frameCount()
                             << "frames to go";
        screenshotHook()->release();
        return;
    }

    // A slow or stuck picture says what it waits for (decision E-915).
    if (m_screenshotWait.elapsed() - m_screenshotWaitLogged >= screenshotWaitLogMs) {
        m_screenshotWaitLogged = m_screenshotWait.elapsed();
        qCInfo(lcScreenshot).nospace() << "Screenshot waits for the world: chunks " << arrived << " of " << expected << ", sections to mesh " << dirty
                                       << ", mesh jobs " << jobs;
    }
}
