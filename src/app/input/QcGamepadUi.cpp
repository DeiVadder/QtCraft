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

// Whether a pad's raw state shows a deliberate use: a button, a trigger or a clearly pushed stick.
bool used(const QcGamepadState &state)
{
    for (std::size_t button = 0; button < qcGamepadButtonCount; ++button) {
        if (state.pressed(QcGamepadButton(button))) {
            return true;
        }
    }
    return state.axis(QcGamepadAxis::LeftTrigger) > QcGamepadMapping::pressThreshold
           || state.axis(QcGamepadAxis::RightTrigger) > QcGamepadMapping::pressThreshold || QcGamepadMapping::clearlyDeflected(state);
}

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
    m_menu.setGate([this] { return screensTakeInput(); });
    connect(&m_menu, &QcGamepadMenuInput::command, this, &QcGamepadUi::onCommand);
    connect(&m_menu, &QcGamepadMenuInput::scrolled, this, [this](float pixels) {
        if (screensTakeInput()) {
            m_navigator.scroll(pixels);
            updateFocus();
        }
    });
    connect(&m_world, &QcGamepadControls::radialChanged, this, &QcGamepadUi::radialChanged);
    connect(&m_navigator, &QcMenuNavigator::keyboardRequested, this, &QcGamepadUi::keyboardRequested);
    connect(&m_navigator, &QcMenuNavigator::acted, this, &QcGamepadUi::updateFocus);
}

QcGamepadUi::~QcGamepadUi()
{
    watch(false);
}

void QcGamepadUi::setSource(QcGamepadSource *source)
{
    if (m_source) {
        disconnect(m_source, nullptr, this, nullptr);
    }
    m_source = source;
    m_menu.setSource(source);
    if (m_source) {
        connect(m_source, &QcGamepadSource::stateChanged, this, &QcGamepadUi::onPadStateChanged);
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

    // Keys, clicks and moves of the mouse hand the input back: only looked at while the pad leads,
    // so the game's other events never pass this filter.
    QCoreApplication *application = QCoreApplication::instance();
    if (application && watching != m_filtering) {
        if (watching) {
            application->installEventFilter(this);
        } else {
            application->removeEventFilter(this);
        }
        m_filtering = watching;
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
    if (!screensTakeInput()) {
        return;
    }

    setActive(true);
    m_navigator.run(command);
}

void QcGamepadUi::onPadStateChanged(int id)
{
    if (m_source && used(m_source->state(id))) {
        setActive(true);
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
