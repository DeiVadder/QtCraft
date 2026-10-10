#include "QcGameInput.h"

#include "QcAudioEngine.h"
#include "QcClientSession.h"
#include "QcGameController.h"
#include "QcMouseLook.h"
#include "QcSessionParts.h"
#include "QcSettingsController.h"
#include "QcSoundKeys.h"

#include <QCoreApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLoggingCategory>
#include <QQuickItem>
#include <QQuickWindow>
#include <QWheelEvent>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

Q_LOGGING_CATEGORY(lcInput, "qtcraft.input")

// The topmost visible item under the mouse, children in front of their parents and higher z first.
QQuickItem *topmostItemAt(QQuickItem *item, QPointF scenePosition)
{
    if (!item->isVisible() || !item->isEnabled()) {
        return nullptr;
    }

    QList<QQuickItem *> children = item->childItems();
    std::ranges::stable_sort(children, [](const QQuickItem *a, const QQuickItem *b) { return a->z() > b->z(); });
    for (QQuickItem *child : std::as_const(children)) {
        if (QQuickItem *hit = topmostItemAt(child, scenePosition)) {
            return hit;
        }
    }
    return item->contains(item->mapFromScene(scenePosition)) ? item : nullptr;
}

} // namespace

QcGameInput::QcGameInput(QcGameController &game, const QcSessionParts &parts, QcSettingsController &settings, QcAudioEngine *audio,
                         QObject *parent)
    : QObject(parent)
    , m_game(game)
    , m_parts(parts)
    , m_settings(settings)
    , m_audio(audio)
{
    // On the application: shortcut overrides go to the focused item, not to the window.
    if (QCoreApplication *application = QCoreApplication::instance()) {
        application->installEventFilter(this);
    }
}

QcMouseCapture::Situation QcGameInput::situation() const
{
    return {worldInput(), m_parts.window && m_parts.window->isActive(), m_backgroundRun, QGuiApplication::mouseButtons()};
}

void QcGameInput::syncCapture()
{
    applyCapture(m_capture.update(situation()));
    if (m_capture.returnedWithoutCapture()) {
        qCInfo(lcInput) << "Mouse capture: back in the world, left free"
                        << (m_backgroundRun ? "(scripted run)" : "(window not active)");
    }
}

void QcGameInput::releaseMouse()
{
    if (m_capture.captured()) {
        m_capture.released();
        applyCapture(QcMouseCapture::Change::Release);
    }
}

void QcGameInput::applyCapture(QcMouseCapture::Change change)
{
    if (change == QcMouseCapture::Change::None) {
        return;
    }

    const bool capture = change == QcMouseCapture::Change::Capture;
    if (capture && m_parts.window) {
        m_parts.window->setCursor(Qt::BlankCursor);
        // With platform deltas the cursor is detached and stays put; elsewhere motion is measured
        // against the window centre.
        if (!QcRelativeMouse::hasPlatformDeltas()) {
            QCursor::setPos(m_parts.window->mapToGlobal(QPoint(m_parts.window->width() / 2, m_parts.window->height() / 2)));
        }
    } else if (m_parts.window) {
        m_parts.window->unsetCursor();
    }
    m_relativeMouse.setCaptured(capture);
    qCInfo(lcInput) << (capture ? "Mouse capture: on" : "Mouse capture: off");
    emit mouseCapturedChanged();
}

QPointF QcGameInput::takeMouseDelta()
{
    return QcRelativeMouse::hasPlatformDeltas() ? m_relativeMouse.takeDelta() : QPointF();
}

void QcGameInput::applyFrame(QPointF mouseDelta, float frameSeconds)
{
    if (m_capture.captured() && !mouseDelta.isNull()) {
        m_parts.session->setIntent(QcMouseLook::apply(m_parts.session->intent(), mouseDelta, m_settings.mouseSensitivity(), m_settings.invertMouse()));
    }
    if (m_game.inputEnabled() && m_game.phase() == u"playing"_s) {
        updateIntentFromKeys(frameSeconds);
    }
}

void QcGameInput::clickIfButton(QPointF scenePosition) const
{
    if (!m_parts.window || !m_audio->isActive()) {
        return;
    }

    for (const QQuickItem *item = topmostItemAt(m_parts.window->contentItem(), scenePosition); item; item = item->parentItem()) {
        // Buttons and the slots of the inventory screens (property clickSound in InventorySlot.qml).
        if (item->inherits("QQuickAbstractButton") || item->property("clickSound").toBool()) {
            m_audio->playUi(QString::fromLatin1(QcSoundKeys::uiClick));
            return;
        }
    }
}

bool QcGameInput::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_parts.window && event->type() == QEvent::MouseButtonRelease && !m_capture.captured()) {
        clickIfButton(static_cast<QMouseEvent *>(event)->scenePosition());
    }

    // Keys of the game come before shortcuts (of the platform or of items below the world).
    if (event->type() == QEvent::ShortcutOverride && worldInput()) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (m_settings.keys()->bindings().actionForPress(key->key(), key->modifiers())) {
            event->accept();
            return true;
        }
    }

    if (watched != m_parts.window || !worldInput()) {
        return QObject::eventFilter(watched, event);
    }
    return filterWorldEvent(event) || QObject::eventFilter(watched, event);
}

bool QcGameInput::filterWorldEvent(QEvent *event)
{
    switch (event->type()) {
    case QEvent::KeyPress:
        handleWorldKey(*static_cast<QKeyEvent *>(event));
        return true;
    case QEvent::KeyRelease:
        handleKeyRelease(*static_cast<QKeyEvent *>(event));
        return true;
    case QEvent::MouseButtonPress:
        handleMousePress(*static_cast<QMouseEvent *>(event));
        return true;
    case QEvent::MouseButtonRelease: {
        const bool captured = m_capture.captured();
        // The release of the click that captured the mouse, or of a button held while a screen
        // closed, does nothing.
        if (m_capture.acceptRelease(static_cast<QMouseEvent *>(event)->button())) {
            handleMouseButton(static_cast<QMouseEvent *>(event)->button(), false);
        }
        return captured;
    }
    case QEvent::Wheel:
        return handleWheel(*static_cast<QWheelEvent *>(event));
    case QEvent::MouseMove:
        return handleMouseMove(*static_cast<QMouseEvent *>(event));
    default:
        return false;
    }
}

void QcGameInput::handleKeyRelease(const QKeyEvent &key)
{
    if (key.isAutoRepeat()) {
        return;
    }

    if (m_settings.keys()->bindings().matches(QcKeyBindings::Action::Mine, key.key())) {
        m_parts.session->setMining(false);
    }
    m_keysDown.remove(key.key());
}

void QcGameInput::handleMousePress(const QMouseEvent &mouse)
{
    if (!m_capture.captured()) {
        applyCapture(m_capture.clickedIntoWorld(situation(), mouse.button()));
        return;
    }

    handleMouseButton(mouse.button(), true);
}

void QcGameInput::handleMouseButton(Qt::MouseButton button, bool pressed) const
{
    if (button == Qt::LeftButton) {
        // A click on a creature hits it; holding on a block mines.
        if (pressed && m_parts.session->attack()) {
            return;
        }
        m_parts.session->setMining(pressed);
    } else if (button == Qt::RightButton && pressed) {
        m_parts.session->useItem();
    }
}

bool QcGameInput::handleWheel(const QWheelEvent &wheel) const
{
    if (!m_capture.captured()) {
        return false;
    }

    const int steps = wheel.angleDelta().y() / 120;
    if (steps != 0) {
        const int slot = (m_parts.session->inventory().selectedHotbarSlot() - steps) % QcInventory::hotbarSize;
        m_parts.session->selectHotbarSlot(slot < 0 ? slot + QcInventory::hotbarSize : slot);
    }
    return true;
}

bool QcGameInput::handleMouseMove(const QMouseEvent &mouse) const
{
    if (!m_capture.captured()) {
        return false;
    }
    // With platform deltas the view turns once per frame in applyFrame().
    if (QcRelativeMouse::hasPlatformDeltas()) {
        return true;
    }

    const QPointF centre(m_parts.window->width() / 2.0, m_parts.window->height() / 2.0);
    const QPointF delta = mouse.position() - centre;
    if (delta.manhattanLength() > 0.0) {
        m_parts.session->setIntent(QcMouseLook::apply(m_parts.session->intent(), delta, m_settings.mouseSensitivity(), m_settings.invertMouse()));
        QCursor::setPos(m_parts.window->mapToGlobal(centre.toPoint()));
    }
    return true;
}

bool QcGameInput::worldInput() const
{
    // Overlays (inventory, pause menu, chat), the handbook and the death screen are QML items that
    // take keyboard and mouse themselves.
    return m_parts.session && m_game.phase() == u"playing"_s && m_game.inputEnabled() && m_game.overlay().isEmpty()
           && !m_game.handbook()->isOpen() && !m_parts.session->isDead();
}

bool QcGameInput::keyDown(QcKeyBindings::Action action) const
{
    return std::ranges::any_of(m_settings.keys()->bindings().keys(action), [this](int key) { return m_keysDown.contains(key); });
}

void QcGameInput::handleWorldKey(const QKeyEvent &key)
{
    if (key.isAutoRepeat()) {
        return;
    }
    using A = QcKeyBindings::Action;
    const std::optional<A> action = m_settings.keys()->bindings().actionForPress(key.key(), key.modifiers());
    if (!action) {
        return;
    }

    if (*action >= A::Hotbar1 && *action <= A::Hotbar9) {
        m_parts.session->selectHotbarSlot(int(*action) - int(A::Hotbar1));
        return;
    }
    switch (*action) {
    case A::Pause:
        m_game.setOverlay(u"pause"_s);
        return;
    case A::Inventory:
        m_game.setOverlay(u"inventory"_s);
        return;
    case A::Chat:
        m_game.setOverlay(u"chat"_s);
        return;
    case A::Command:
        m_game.openChat(u"/"_s);
        return;
    case A::Use:
        m_parts.session->useItem();
        return;
    case A::Mine:
        m_parts.session->setMining(true);
        return;
    case A::Handbook:
        // The page of what the crosshair points at, else the page shown last.
        m_game.handbook()->open(m_game.hud()->targetName());
        return;
    case A::CameraView: {
        // Cycles first person, behind, front; the choice is kept in the settings.
        const QcCameraMode next = QcViewCamera::next(m_parts.session->cameraMode());
        m_settings.setCameraMode(QString::fromLatin1(QcViewCamera::name(next)));
        m_parts.session->setCameraMode(next);
        m_settings.saveSettings();
        return;
    }
    case A::DebugOverlay:
        m_settings.setShowDebug(!m_settings.showDebug());
        m_settings.saveSettings();
        return;
    case A::Labels:
        m_settings.setShowLabels(!m_settings.showLabels());
        m_settings.saveSettings();
        return;
    case A::Drop:
        // With Ctrl (on a Mac Control) the whole stack.
        m_parts.session->dropItem(key.modifiers().testFlag(QcKeyBindings::stackModifier(m_settings.keys()->bindings().platform())));
        return;
    default:
        // Movement and look keys count while held (updateIntentFromKeys()).
        m_keysDown.insert(key.key());
        return;
    }
}

void QcGameInput::updateIntentFromKeys(float frameSeconds) const
{
    using A = QcKeyBindings::Action;
    QcPlayerInput intent = m_parts.session->intent();
    intent.forward = float(int(keyDown(A::Forward)) - int(keyDown(A::Back)));
    intent.strafe = float(int(keyDown(A::Right)) - int(keyDown(A::Left)));
    intent.jump = keyDown(A::Jump);
    intent.sneak = keyDown(A::Sneak);
    intent.sprint = keyDown(A::Sprint);
    const float look = keyboardLookSpeed * frameSeconds;
    intent.yaw += look * float(int(keyDown(A::LookRight)) - int(keyDown(A::LookLeft)));
    intent.pitch = std::clamp(intent.pitch + look * float(int(keyDown(A::LookUp)) - int(keyDown(A::LookDown))), -89.9f, 89.9f);
    m_parts.session->setIntent(intent);
}
