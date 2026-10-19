#include "app/input/QcGamepadUi.h"

#include "app/input/QcFocusCandidates.h"
#include "clientcore/input/QcGamepadControls.h"
#include "clientcore/input/QcGamepadMapping.h"
#include "clientcore/input/QcGamepadSource.h"

#include <QCoreApplication>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <QtGui/private/qguiapplication_p.h>

using namespace Qt::StringLiterals;

namespace {

// A moved mouse counts from this far (pixels, Manhattan) from where it was last seen.
constexpr double minimumMouseTravel = 8.0;
// The look of the world refreshes the scope this often (frames) besides focus changes.
constexpr int scopeRefreshFrames = 15;

// Input of the keyboard or the mouse buttons and wheel that makes them the device in use.
bool keyOrClick(const QObject *watched, const QEvent *event)
{
    switch (event->type()) {
    case QEvent::KeyPress:
    case QEvent::MouseButtonPress:
    case QEvent::Wheel:
        return watched->isWindowType();
    default:
        return false;
    }
}

} // namespace

QcGamepadUi::QcGamepadUi(QcGamepadControls &world, Gate gate, QObject *parent)
    : QObject(parent)
    , m_world(world)
    , m_screensTakeInput(std::move(gate))
{
    // While a button is captured the screens ignore the pad, and afterwards they wait until every
    // pad is neutral again (the captured button is still held).
    m_menu.setGate([this] { return !m_capturing && screensTakeInput(); });
    connect(&m_menu, &QcGamepadMenuInput::command, this, &QcGamepadUi::onCommand);
    connect(&m_menu, &QcGamepadMenuInput::scrolled, this, [this](float pixels) {
        if (screensTakeInput()) {
            m_navigator.scroll(pixels);
            updateFocus();
        }
    });
    connect(&m_world, &QcGamepadControls::radialChanged, this, &QcGamepadUi::radialChanged);
    connect(&m_world, &QcGamepadControls::profileChanged, this, &QcGamepadUi::actionsChanged);
    connect(&m_navigator, &QcMenuNavigator::keyboardRequested, this, &QcGamepadUi::keyboardRequested);
    connect(&m_navigator, &QcMenuNavigator::acted, this, &QcGamepadUi::updateFocus);
}

QcGamepadUi::~QcGamepadUi()
{
    m_capturing = false;
    watch(false);
}

void QcGamepadUi::setSource(QcGamepadSource *source)
{
    if (m_source) {
        disconnect(m_source, nullptr, this, nullptr);
    }
    m_source = source;
    m_menu.setSource(source);
    m_roster.setSource(source);
    if (m_source) {
        connect(m_source, &QcGamepadSource::stateChanged, this, &QcGamepadUi::onPadStateChanged);
        connect(m_source, &QcGamepadSource::padConnected, this, &QcGamepadUi::onPadConnected);
        connect(m_source, &QcGamepadSource::padDisconnected, this, &QcGamepadUi::onPadDisconnected);
    }
}

void QcGamepadUi::setWindow(QQuickWindow *window)
{
    if (m_window == window) {
        return;
    }

    if (m_window) {
        disconnect(m_window, nullptr, this, nullptr);
    }
    m_window = window;
    m_navigator.setWindow(window);
    if (m_window) {
        connect(m_window, &QQuickWindow::activeFocusItemChanged, this, &QcGamepadUi::onActiveFocusChanged);
    }
    watch(m_active);
    updateFocus();
}

void QcGamepadUi::watch(bool watching)
{
    // Lists scroll and screens move: the ring follows every frame while the pad leads. Each frame
    // only maps the focused item; the scope is looked up again after focus changes and every
    // scopeRefreshFrames frames (a screen that opened without taking the focus).
    disconnect(m_frameConnection);
    if (watching && m_window) {
        m_frameConnection = connect(m_window, &QQuickWindow::afterAnimating, this, &QcGamepadUi::onFrame);
    }

    // Keys, clicks and moves of the mouse hand the input back: only looked at while the pad leads
    // (or a button is captured, which a key or a click cancels), so the game's other events never
    // pass this filter.
    QCoreApplication *application = QCoreApplication::instance();
    const bool filtering = watching || m_capturing;
    if (application && filtering != m_filtering) {
        if (filtering) {
            application->installEventFilter(this);
        } else {
            application->removeEventFilter(this);
        }
        m_filtering = filtering;
    }
}

void QcGamepadUi::onFrame()
{
    ++m_hookCalls;
    if (++m_framesSinceScope >= scopeRefreshFrames) {
        m_framesSinceScope = 0;
        m_navigator.invalidateScope();
    }
    updateFocus();
}

bool QcGamepadUi::radialOpen() const
{
    return m_world.radialOpen();
}

int QcGamepadUi::radialSlot() const
{
    return m_world.radialSlot();
}

void QcGamepadUi::setActive(bool active)
{
    if (m_active == active) {
        return;
    }

    m_active = active;
    if (!active) {
        m_world.keyboardOrMouseUsed();
    }
    // A move of the mouse counts from where Qt last saw it when the pad takes over (infinite before
    // the first mouse event).
    const QPointF lastMouse = QGuiApplicationPrivate::lastCursorPosition;
    m_lastMouse = active && qIsFinite(lastMouse.x()) ? std::optional(lastMouse) : std::nullopt;
    watch(active);
    emit activeChanged();
    onActiveFocusChanged();
}

bool QcGamepadUi::screensTakeInput() const
{
    return m_window && m_screensTakeInput && m_screensTakeInput() && (m_ignoreWindowFocus || m_window->isActive());
}

void QcGamepadUi::onCommand(QcGamepadMenuInput::Command command)
{
    if (m_capturing || !screensTakeInput()) {
        return;
    }

    setActive(true);
    m_navigator.run(command);
}

void QcGamepadUi::onPadStateChanged(int id)
{
    if (m_capturing) {
        captureFrom(id);
    }
    if (m_source && QcGamepadMapping::used(m_source->state(id))) {
        setActive(true);
    }
}

QVariantMap QcGamepadUi::actions() const
{
    using QcGamepadMapping::Action;
    QVariantMap result;
    for (std::size_t index = 0; index < QcGamepadMapping::actionCount; ++index) {
        const auto action = Action(index);
        const std::optional<QcGamepadButton> button = m_world.profile().bindings.button(action);
        // The triggers' actions are axes; the others without a button are "".
        const QString trigger = action == Action::Mine ? u"rightTrigger"_s : action == Action::Use ? u"leftTrigger"_s : QString();
        result.insert(QcGamepadMapping::actionName(action), button ? QcGamepadNames::button(*button) : trigger);
    }
    return result;
}

void QcGamepadUi::captureButton()
{
    m_captureStates.clear();
    m_captureJoined.clear();
    if (m_source) {
        const QList<QcGamepadInfo> pads = m_source->pads();
        for (const QcGamepadInfo &pad : pads) {
            m_captureStates.insert(pad.id, m_source->state(pad.id));
        }
    }
    if (!m_capturing) {
        m_capturing = true;
        watch(m_active);
        emit capturingChanged();
    }
}

void QcGamepadUi::cancelCapture()
{
    if (m_capturing) {
        m_capturing = false;
        m_captureStates.clear();
        m_captureJoined.clear();
        watch(m_active);
        emit capturingChanged();
    }
}

void QcGamepadUi::onPadConnected(int id)
{
    // A pad that connects during the capture reports the buttons held on it with its first state:
    // that state is where it starts, not a press.
    if (m_capturing) {
        m_captureJoined.insert(id);
    }
}

void QcGamepadUi::onPadDisconnected(int id)
{
    m_captureStates.remove(id);
    m_captureJoined.remove(id);
    // Without a pad no button can come: the capture ends.
    if (m_capturing && (!m_source || m_source->pads().isEmpty())) {
        cancelCapture();
    }
}

void QcGamepadUi::captureFrom(int id)
{
    const QcGamepadState before = m_captureStates.value(id);
    const QcGamepadState now = m_source ? m_source->state(id) : QcGamepadState();
    m_captureStates.insert(id, now);
    if (m_captureJoined.remove(id)) {
        return;
    }
    for (std::size_t index = 0; index < qcGamepadButtonCount; ++index) {
        const auto button = QcGamepadButton(index);
        if (now.pressed(button) && !before.pressed(button) && QcGamepadMapping::bindable(button)) {
            cancelCapture();
            emit buttonCaptured(QcGamepadNames::button(button));
            return;
        }
    }
}

void QcGamepadUi::updateFocus()
{
    QQuickItem *item = m_active && screensTakeInput() ? m_navigator.focusItem() : nullptr;
    // Clipped to the lists around it (empty while scrolled away; the ring hides then).
    const QRectF rect = item ? QcFocusCandidates::visibleRect(item) : QRectF();
    QQuickItem *layer = nullptr;
    for (QQuickItem *parent = item; parent && !layer; parent = parent->parentItem()) {
        layer = parent->inherits("QQuickPopupItem") ? parent : nullptr;
    }
    if (m_focusShown != (item != nullptr) || m_focusRect != rect || m_focusLayer != layer) {
        m_focusShown = item != nullptr;
        m_focusRect = rect;
        m_focusLayer = layer;
        emit focusChanged();
    }
}

void QcGamepadUi::onActiveFocusChanged()
{
    m_navigator.invalidateScope();
    updateFocus();
    if (!m_active || m_navigator.focusItem()) {
        return;
    }

    // A screen opened or its focused item went away while the pad leads: the focus starts again
    // once the screen has settled (after its own focus calls).
    QTimer::singleShot(0, this, [this] {
        m_navigator.invalidateScope();
        if (m_active && screensTakeInput() && !m_navigator.focusItem()) {
            m_navigator.focusStart();
        }
        updateFocus();
    });
}

QString QcGamepadUi::focusDescription() const
{
    const QQuickItem *item = m_navigator.focusItem();
    if (!item) {
        return {};
    }
    if (!item->objectName().isEmpty()) {
        return item->objectName();
    }
    if (const QVariant slot = item->property("slot"); slot.isValid()) {
        return u"slot:%1"_s.arg(slot.toInt());
    }
    // A QML type's class is "<Type>_QMLTYPE_<n>".
    const QString type = QString::fromLatin1(item->metaObject()->className()).section(u"_QML"_s, 0, 0);
    return u"%1:%2"_s.arg(type, item->property("text").toString());
}

void QcGamepadUi::requestKeyboard(QQuickItem *field)
{
    if (m_active && field) {
        emit keyboardRequested(field);
    }
}

bool QcGamepadUi::eventFilter(QObject *watched, QEvent *event)
{
    ++m_hookCalls;
    // A key or a click cancels a capture wherever the focus is, and does nothing else.
    if (m_capturing && keyOrClick(watched, event)) {
        cancelCapture();
        return true;
    }
    if (!m_active || m_navigator.sending()) {
        return QObject::eventFilter(watched, event);
    }

    if (event->type() == QEvent::MouseMove && watched->isWindowType()) {
        if (mouseMoved(static_cast<const QWindow &>(*watched), static_cast<const QMouseEvent &>(*event))) {
            setActive(false);
        }
    } else if (keyOrClick(watched, event)) {
        setActive(false);
    }
    return QObject::eventFilter(watched, event);
}

bool QcGamepadUi::mouseMoved(const QWindow &window, const QMouseEvent &move)
{
    // The captured mouse is put back to the window's centre (QcGameInput, without platform deltas):
    // that move is the game's own.
    const QPointF centre(window.width() / 2.0, window.height() / 2.0);
    if ((move.position() - centre).manhattanLength() < 1.0) {
        return false;
    }

    const QPointF position = move.globalPosition();
    const bool travelled = !m_lastMouse || (position - *m_lastMouse).manhattanLength() >= minimumMouseTravel;
    if (travelled) {
        m_lastMouse = position;
    }
    return travelled;
}
