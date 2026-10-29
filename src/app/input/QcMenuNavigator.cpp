#include "app/input/QcMenuNavigator.h"

#include "app/input/QcFocusCandidates.h"

#include <QLineF>
#include <QMetaEnum>
#include <QMetaMethod>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtGui/private/qhighdpiscaling_p.h>
#include <qpa/qwindowsysteminterface.h>

#include <algorithm>
#include <deque>
#include <iterator>

using namespace Qt::StringLiterals;

namespace {

using Direction = QcSpatialNavigation::Direction;

// Margin kept between an item scrolled into view and the edge of its list.
constexpr double visibleMargin = 8.0;
// Presses are this far apart on the clock of the sent events, so two quick clicks never count as a
// double click.
constexpr ulong pressSpacingMs = 1000;

std::optional<Direction> directionOf(QcGamepadMenuInput::Command command)
{
    using Command = QcGamepadMenuInput::Command;
    switch (command) {
    case Command::Up:
        return Direction::Up;
    case Command::Down:
        return Direction::Down;
    case Command::Left:
        return Direction::Left;
    case Command::Right:
        return Direction::Right;
    default:
        return std::nullopt;
    }
}

// Whether `item` is a back entry (its QML property padLeftOnly): never where the focus starts, and
// reached only by moving left, or up from within its own group (the first category below it).
bool leftOnly(const QQuickItem *item)
{
    return item->property("padLeftOnly").toBool();
}

// Whether a move from `current` in `direction` may end on the back entry `item`.
bool reachesLeftOnly(const QQuickItem *item, const QQuickItem *current, Direction direction)
{
    return direction == Direction::Left || (direction == Direction::Up && QcFocusCandidates::contains(item->parentItem(), current));
}

// Calls `object`'s QML function gamepadCommand(name) when it has one; whether it took the command.
bool callHandler(QObject *object, const QString &name)
{
    const QMetaObject *meta = object->metaObject();
    for (int index = 0; index < meta->methodCount(); ++index) {
        const QMetaMethod method = meta->method(index);
        if (method.name() != "gamepadCommand" || method.parameterCount() != 1) {
            continue;
        }

        const bool stringArgument = method.parameterMetaType(0) == QMetaType::fromType<QString>();
        if (method.returnMetaType() == QMetaType::fromType<bool>()) {
            bool taken = false;
            const bool called = stringArgument ? method.invoke(object, Qt::DirectConnection, qReturnArg(taken), name)
                                               : method.invoke(object, Qt::DirectConnection, qReturnArg(taken), QVariant(name));
            return called && taken;
        }
        QVariant taken;
        const bool called = stringArgument ? method.invoke(object, Qt::DirectConnection, qReturnArg(taken), name)
                                           : method.invoke(object, Qt::DirectConnection, qReturnArg(taken), QVariant(name));
        return called && taken.toBool();
    }
    return false;
}

// The first shown item below `root` (breadth first) that inherits `className`.
QQuickItem *findShown(QQuickItem *root, const char *className)
{
    std::deque<QQuickItem *> pending{root};
    while (!pending.empty()) {
        QQuickItem *item = pending.front();
        pending.pop_front();
        if (item != root && item->inherits(className)) {
            return item;
        }
        const QList<QQuickItem *> children = item->childItems();
        std::ranges::copy_if(children, std::back_inserter(pending), [](const QQuickItem *child) { return child->isVisible(); });
    }
    return nullptr;
}

QQuickItem *ancestorInheriting(QQuickItem *item, const char *className, const QQuickItem *stop)
{
    for (QQuickItem *current = item ? item->parentItem() : nullptr; current; current = current->parentItem()) {
        if (current->inherits(className)) {
            return current;
        }
        if (current == stop) {
            break;
        }
    }
    return nullptr;
}

// Moves a flickable's content by `delta` on one axis ("X" or "Y"), within its bounds.
void scrollBy(QQuickItem *flickable, double delta, bool vertical)
{
    const char *position = vertical ? "contentY" : "contentX";
    const double origin = flickable->property(vertical ? "originY" : "originX").toDouble();
    const double content = flickable->property(vertical ? "contentHeight" : "contentWidth").toDouble();
    const double view = vertical ? flickable->height() : flickable->width();
    const double highest = std::max(origin, origin + content - view);
    const double target = std::clamp(flickable->property(position).toDouble() + delta, origin, highest);
    flickable->setProperty(position, target);
}

} // namespace

QcMenuNavigator::QcMenuNavigator(QObject *parent)
    : QObject(parent)
{
    m_clock.start();
}

QcMenuNavigator::~QcMenuNavigator() = default;

void QcMenuNavigator::setWindow(QQuickWindow *window)
{
    m_window = window;
    invalidateScope();
}

QQuickItem *QcMenuNavigator::currentScope() const
{
    if (!m_scopeValid) {
        m_scope = m_window ? QcFocusCandidates::scope(m_window) : nullptr;
        m_scopeValid = true;
    }
    return m_scope;
}

QString QcMenuNavigator::commandName(Command command)
{
    QString name = QString::fromLatin1(QMetaEnum::fromType<Command>().valueToKey(static_cast<int>(command)));
    if (!name.isEmpty()) {
        name[0] = name[0].toLower();
    }
    return name;
}

QQuickItem *QcMenuNavigator::focusItem() const
{
    if (!m_window) {
        return nullptr;
    }

    const QQuickItem *scope = currentScope();
    QQuickItem *focused = m_window->activeFocusItem();
    if (!scope || !focused || !QcFocusCandidates::contains(scope, focused)) {
        return nullptr;
    }
    // The focus may sit on a text field's inner editor or a control's content: its candidate counts.
    for (QQuickItem *item = focused; item && item != scope; item = item->parentItem()) {
        if (QcFocusCandidates::isCandidate(item)) {
            return item;
        }
    }
    return nullptr;
}

void QcMenuNavigator::run(Command command)
{
    invalidateScope();
    QQuickItem *scope = m_window ? currentScope() : nullptr;
    if (!scope) {
        return;
    }

    QQuickItem *current = focusItem();
    QQuickItem *focused = m_window->activeFocusItem();
    QQuickItem *asked = focused && QcFocusCandidates::contains(scope, focused) ? focused : scope;
    if (offerToScreen(scope, asked, command)) {
        emit acted();
        return;
    }

    const bool back = command == Command::Back || command == Command::Start;
    if (!current) {
        // The first press shows where the focus is; going back works at once.
        focusInScope(scope);
        if (back) {
            sendKey(Qt::Key_Escape);
        }
        emit acted();
        return;
    }

    if (const std::optional<Direction> direction = directionOf(command)) {
        if (!adjust(current, *direction)) {
            move(scope, current, *direction);
        }
    } else if (command == Command::Confirm) {
        confirm(current);
    } else if (command == Command::Secondary) {
        click(current, Qt::RightButton, Qt::NoModifier);
    } else if (command == Command::Tertiary) {
        click(current, Qt::LeftButton, Qt::ShiftModifier);
    } else if (back) {
        sendKey(Qt::Key_Escape);
    } else if (command == Command::TabPrevious || command == Command::TabNext) {
        switchTab(scope, current, command == Command::TabNext ? 1 : -1);
    }
    emit acted();
}

bool QcMenuNavigator::offerToScreen(const QQuickItem *scope, QQuickItem *focus, Command command)
{
    const QString name = commandName(command);
    for (QQuickItem *item = focus; item; item = item->parentItem()) {
        if (callHandler(item, name)) {
            return true;
        }
        if (item == scope) {
            break;
        }
    }
    return false;
}

void QcMenuNavigator::focusInScope(const QQuickItem *scope)
{
    if (focusItem()) {
        return;
    }

    QList<QQuickItem *> candidates = QcFocusCandidates::candidates(scope);
    if (QQuickItem *remembered = m_lastFocus.value(scope); remembered && candidates.contains(remembered) && !leftOnly(remembered)) {
        focus(remembered);
        return;
    }

    // A screen's default button (highlighted) first, else the top left; never a back entry.
    candidates.removeIf(leftOnly);
    const auto highlighted = std::ranges::find_if(candidates, [](const QQuickItem *item) { return item->property("highlighted").toBool(); });
    if (highlighted != candidates.end()) {
        focus(*highlighted);
        return;
    }

    QList<QRectF> rects;
    for (const QQuickItem *item : std::as_const(candidates)) {
        rects.append(QcFocusCandidates::sceneRect(item));
    }
    if (const std::optional<qsizetype> start = QcSpatialNavigation::first(rects)) {
        focus(candidates[*start]);
    }
}

void QcMenuNavigator::focusStart()
{
    invalidateScope();
    if (const QQuickItem *scope = m_window ? currentScope() : nullptr) {
        focusInScope(scope);
    }
}

void QcMenuNavigator::hoverFocus()
{
    if (QQuickItem *item = focusItem()) {
        hover(item);
    }
}

void QcMenuNavigator::move(const QQuickItem *scope, QQuickItem *current, Direction direction)
{
    // Rows scrolled out of view are reached only from within their list, which scrolls to them.
    const QQuickItem *list = QcFocusCandidates::flickableAround(current);
    QList<QQuickItem *> candidates = QcFocusCandidates::candidates(scope);
    candidates.removeIf([current, list, direction](const QQuickItem *item) {
        return item == current || (leftOnly(item) && !reachesLeftOnly(item, current, direction))
               || (!QcFocusCandidates::inView(item) && (!list || QcFocusCandidates::flickableAround(item) != list));
    });
    QList<QRectF> rects;
    for (const QQuickItem *item : std::as_const(candidates)) {
        rects.append(QcFocusCandidates::sceneRect(item));
    }

    if (const std::optional<qsizetype> next = QcSpatialNavigation::next(QcFocusCandidates::sceneRect(current), direction, rects)) {
        focus(candidates[*next]);
    }
}

bool QcMenuNavigator::adjust(QQuickItem *item, Direction direction)
{
    if (direction != Direction::Left && direction != Direction::Right) {
        return false;
    }

    // A slider takes its arrow keys, and reports the change as for the mouse (moved()). A spin box
    // steps like its buttons; an editable one has the focus on its editor, which would take the keys.
    const bool right = direction == Direction::Right;
    if (item->inherits("QQuickSlider") || item->inherits("QQuickRangeSlider")) {
        if (item->property("orientation").toInt() != Qt::Horizontal) {
            return false;
        }
        sendKey(right ? Qt::Key_Right : Qt::Key_Left);
        return true;
    }
    QQuickItem *spinBox = item->inherits("QQuickSpinBox") ? item : item->parentItem();
    if (spinBox && spinBox->inherits("QQuickSpinBox")) {
        QMetaObject::invokeMethod(spinBox, right ? "increase" : "decrease");
        QMetaObject::invokeMethod(spinBox, "valueModified");
        return true;
    }
    return false;
}

void QcMenuNavigator::focus(QQuickItem *item)
{
    item->forceActiveFocus(Qt::TabFocusReason);
    if (const QQuickItem *scope = currentScope()) {
        // Scopes and items that went away are forgotten.
        m_lastFocus.removeIf([](const QHash<const QQuickItem *, QPointer<QQuickItem>>::iterator entry) { return entry.value().isNull(); });
        m_lastFocus.insert(scope, item);
    }
    ensureVisible(item);
    hover(item);
}

void QcMenuNavigator::confirm(QQuickItem *item)
{
    if (QcFocusCandidates::isTextInput(item)) {
        emit keyboardRequested(item);
        return;
    }
    // A slider moves with left and right; a click would jump it to the middle.
    if (item->inherits("QQuickSlider") || item->inherits("QQuickRangeSlider")) {
        return;
    }
    click(item, Qt::LeftButton, Qt::NoModifier);
}

void QcMenuNavigator::switchTab(const QQuickItem *scope, QQuickItem *current, int step)
{
    for (QQuickItem *item = current; item; item = item->parentItem()) {
        if (QQuickItem *tabs = findShown(item, "QQuickTabBar")) {
            const int count = tabs->property("count").toInt();
            if (count > 0) {
                tabs->setProperty("currentIndex", (tabs->property("currentIndex").toInt() + step + count) % count);
            }
            return;
        }
        if (item == scope) {
            return;
        }
    }
}

void QcMenuNavigator::click(QQuickItem *item, Qt::MouseButton button, Qt::KeyboardModifiers modifiers)
{
    ensureVisible(item);
    const QPointF centre = QcFocusCandidates::sceneRect(item).center();
    m_timestamp = std::max(m_timestamp, static_cast<ulong>(m_clock.elapsed())) + pressSpacingMs;
    sendMouse(centre, Qt::NoButton, Qt::NoButton, QEvent::MouseMove, modifiers);
    sendMouse(centre, button, button, QEvent::MouseButtonPress, modifiers);
    sendMouse(centre, Qt::NoButton, button, QEvent::MouseButtonRelease, modifiers);
}

void QcMenuNavigator::hover(QQuickItem *item)
{
    sendMouse(QcFocusCandidates::sceneRect(item).center(), Qt::NoButton, Qt::NoButton, QEvent::MouseMove, Qt::NoModifier);
}

void QcMenuNavigator::sendMouse(QPointF scenePosition, Qt::MouseButtons buttons, Qt::MouseButton button, QEvent::Type type,
                                Qt::KeyboardModifiers modifiers)
{
    if (!m_window) {
        return;
    }

    const QPointF position(std::clamp(scenePosition.x(), 0.0, static_cast<double>(m_window->width() - 1)),
                           std::clamp(scenePosition.y(), 0.0, static_cast<double>(m_window->height() - 1)));
    // The platform's input path takes positions in device pixels (QT_SCALE_FACTOR, high DPI).
    const QPointF native = QHighDpi::toNativeLocalPosition(position, m_window.data());
    const QPointF nativeGlobal = QHighDpi::toNativeGlobalPosition(m_window->mapToGlobal(position), m_window.data());
    m_sending = true;
    QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(m_window, nextTimestamp(), native, nativeGlobal,
                                                                                           buttons, button, type, modifiers);
    m_sending = false;
}

ulong QcMenuNavigator::nextTimestamp()
{
    m_timestamp = std::max(m_timestamp + 1, static_cast<ulong>(m_clock.elapsed()));
    return m_timestamp;
}

void QcMenuNavigator::sendKey(int key)
{
    if (!m_window) {
        return;
    }

    m_sending = true;
    QWindowSystemInterface::handleKeyEvent<QWindowSystemInterface::SynchronousDelivery>(m_window, nextTimestamp(), QEvent::KeyPress, key,
                                                                                         Qt::NoModifier);
    QWindowSystemInterface::handleKeyEvent<QWindowSystemInterface::SynchronousDelivery>(m_window, nextTimestamp(), QEvent::KeyRelease, key,
                                                                                         Qt::NoModifier);
    m_sending = false;
}

QQuickItem *QcMenuNavigator::scrolledFlickable()
{
    invalidateScope();
    QQuickItem *scope = m_window ? currentScope() : nullptr;
    if (!scope) {
        return nullptr;
    }

    QQuickItem *flickable = ancestorInheriting(focusItem(), "QQuickFlickable", scope);
    return flickable ? flickable : findShown(scope, "QQuickFlickable");
}

void QcMenuNavigator::scroll(float pixels)
{
    QQuickItem *flickable = pixels != 0.0f ? scrolledFlickable() : nullptr;
    if (flickable) {
        scrollBy(flickable, static_cast<double>(pixels), true);
        keepFocusInView(flickable);
    }
}

void QcMenuNavigator::scrollSideways(float pixels)
{
    QQuickItem *flickable = pixels != 0.0f ? scrolledFlickable() : nullptr;
    // A list that snaps to its rows moves row by row (its own keys), never to a place between.
    const bool snaps = flickable && flickable->inherits("QQuickItemView") && flickable->property("snapMode").toInt() != 0;
    if (flickable && !snaps) {
        scrollBy(flickable, static_cast<double>(pixels), false);
        keepFocusInView(flickable);
    }
}

void QcMenuNavigator::keepFocusInView(const QQuickItem *flickable)
{
    const QQuickItem *current = focusItem();
    if (!current || !QcFocusCandidates::contains(flickable, current) || QcFocusCandidates::wholeInView(current)) {
        return;
    }

    // Only the list's own rows: the walk stays inside it.
    const QPointF from = QcFocusCandidates::sceneRect(current).center();
    QQuickItem *nearest = nullptr;
    double nearestDistance = 0.0;
    const QList<QQuickItem *> rows = QcFocusCandidates::candidates(flickable);
    for (QQuickItem *row : rows) {
        const double distance = QLineF(from, QcFocusCandidates::sceneRect(row).center()).length();
        if (QcFocusCandidates::wholeInView(row) && (!nearest || distance < nearestDistance)) {
            nearest = row;
            nearestDistance = distance;
        }
    }
    if (nearest) {
        focus(nearest);
    }
}

void QcMenuNavigator::ensureVisible(QQuickItem *item)
{
    for (QQuickItem *flickable = ancestorInheriting(item, "QQuickFlickable", nullptr); flickable;
         flickable = ancestorInheriting(flickable, "QQuickFlickable", nullptr)) {
        const QRectF bounds = item->mapRectToItem(flickable, QRectF(0.0, 0.0, item->width(), item->height()));
        if (bounds.top() < 0.0) {
            scrollBy(flickable, bounds.top() - visibleMargin, true);
        } else if (bounds.bottom() > flickable->height()) {
            scrollBy(flickable, std::min(bounds.top(), bounds.bottom() - flickable->height() + visibleMargin), true);
        }
        if (bounds.left() < 0.0) {
            scrollBy(flickable, bounds.left() - visibleMargin, false);
        } else if (bounds.right() > flickable->width()) {
            scrollBy(flickable, std::min(bounds.left(), bounds.right() - flickable->width() + visibleMargin), false);
        }
    }
}
