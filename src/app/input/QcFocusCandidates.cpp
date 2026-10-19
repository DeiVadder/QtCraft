#include "app/input/QcFocusCandidates.h"

#include <QQuickItem>
#include <QQuickWindow>

#include <algorithm>
#include <ranges>

namespace QcFocusCandidates {

namespace {

// Qt::TabFocus in a control's focusPolicy.
constexpr int tabFocus = Qt::TabFocus;

// Children in the order they are drawn (by z, then by order), topmost last.
QList<QQuickItem *> paintOrder(const QQuickItem *item)
{
    QList<QQuickItem *> children = item->childItems();
    std::ranges::stable_sort(children, {}, [](const QQuickItem *child) { return child->z(); });
    return children;
}

bool shown(const QQuickItem *item)
{
    return item->isVisible() && item->opacity() > 0.0;
}

// The shown candidates below `item`, depth first.
[[nodiscard]] QList<QQuickItem *> collect(const QQuickItem *item)
{
    QList<QQuickItem *> found;
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *child : children) {
        if (!shown(child)) {
            continue;
        }
        if (isCandidate(child)) {
            found.append(child);
        }
        // A text field's parts are not candidates of their own.
        if (!isTextInput(child)) {
            found.append(collect(child));
        }
    }
    return found;
}

// Like collect(), but stops at the first candidate.
bool hasCandidate(const QQuickItem *item)
{
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *child : children) {
        if (!shown(child)) {
            continue;
        }
        if (isCandidate(child) || (!isTextInput(child) && hasCandidate(child))) {
            return true;
        }
    }
    return false;
}

// The window's overlay, which holds the open popups (Qt Quick Controls).
QQuickItem *overlay(QQuickWindow *window)
{
    const QList<QQuickItem *> children = window->contentItem()->childItems();
    for (QQuickItem *child : children) {
        if (child->inherits("QQuickOverlay")) {
            return child;
        }
    }
    return nullptr;
}

QQuickItem *topmostWithCandidates(const QList<QQuickItem *> &items, bool popups)
{
    for (QQuickItem *item : items | std::views::reverse) {
        const bool isPopup = item->inherits("QQuickPopupItem");
        if (shown(item) && isPopup == popups && !item->inherits("QQuickOverlay") && hasCandidate(item)) {
            return item;
        }
    }
    return nullptr;
}

} // namespace

QQuickItem *scope(QQuickWindow *window)
{
    if (!window) {
        return nullptr;
    }

    if (const QQuickItem *popups = overlay(window); popups && shown(popups)) {
        if (QQuickItem *popup = topmostWithCandidates(paintOrder(popups), true)) {
            return popup;
        }
    }

    // An ApplicationWindow's content item holds the screens; a plain window's is its root.
    auto *screens = window->property("contentItem").value<QQuickItem *>();
    if (!screens) {
        screens = window->contentItem();
    }
    return topmostWithCandidates(paintOrder(screens), false);
}

QList<QQuickItem *> candidates(const QQuickItem *scope)
{
    return scope ? collect(scope) : QList<QQuickItem *>{};
}

bool isTextInput(QQuickItem *item)
{
    if (!item || !(item->inherits("QQuickTextInput") || item->inherits("QQuickTextEdit"))) {
        return false;
    }
    return !item->property("readOnly").toBool();
}

bool isCandidate(QQuickItem *item)
{
    if (!item || !item->isVisible() || !item->isEnabled() || item->width() <= 0.0 || item->height() <= 0.0) {
        return false;
    }

    if (isTextInput(item)) {
        return true;
    }
    // Rows of lists (item delegates, also the entries of a combo box) take no focus of their own:
    // the view does. For the pad they are what is chosen.
    const QVariant policy = item->property("focusPolicy");
    return item->activeFocusOnTab() || (policy.isValid() && (policy.toInt() & tabFocus) != 0) || item->inherits("QQuickItemDelegate");
}

bool contains(const QQuickItem *ancestor, const QQuickItem *item)
{
    for (const QQuickItem *current = item; current; current = current->parentItem()) {
        if (current == ancestor) {
            return true;
        }
    }
    return false;
}

QQuickItem *flickableAround(const QQuickItem *item)
{
    for (QQuickItem *current = item ? item->parentItem() : nullptr; current; current = current->parentItem()) {
        if (current->inherits("QQuickFlickable")) {
            return current;
        }
    }
    return nullptr;
}

bool inView(const QQuickItem *item)
{
    const QRectF bounds = sceneRect(item);
    for (const QQuickItem *flickable = flickableAround(item); flickable; flickable = flickableAround(flickable)) {
        if (!sceneRect(flickable).intersects(bounds)) {
            return false;
        }
    }
    return true;
}

QRectF sceneRect(const QQuickItem *item)
{
    return item->mapRectToScene(QRectF(0.0, 0.0, item->width(), item->height()));
}

QRectF visibleRect(const QQuickItem *item)
{
    QRectF bounds = sceneRect(item);
    for (const QQuickItem *flickable = flickableAround(item); flickable && !bounds.isEmpty(); flickable = flickableAround(flickable)) {
        bounds = bounds.intersected(sceneRect(flickable));
    }
    return bounds;
}

bool wholeInView(const QQuickItem *item)
{
    return visibleRect(item) == sceneRect(item);
}

} // namespace QcFocusCandidates
