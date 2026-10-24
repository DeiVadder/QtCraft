#include "app/hooks/QcPendingScreens.h"

#include <QQuickItem>
#include <QQuickWindow>

#include <algorithm>

bool QcPendingScreens::any(const QQuickWindow &window)
{
    const QList<QQuickItem *> items = window.findChildren<QQuickItem *>();
    return std::ranges::any_of(items, [](const QQuickItem *item) { return item->property("screenPending").toBool(); });
}
