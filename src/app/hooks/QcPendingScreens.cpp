// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/hooks/QcPendingScreens.h"

#include <QQuickItem>
#include <QQuickWindow>

#include <algorithm>

bool QcPendingScreens::any(const QQuickWindow &window)
{
    const QList<QQuickItem *> items = window.findChildren<QQuickItem *>();
    return std::ranges::any_of(items, [](const QQuickItem *item) { return item->property("screenPending").toBool(); });
}
