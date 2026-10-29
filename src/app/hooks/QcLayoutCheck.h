// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QRectF>
#include <QStringList>

class QQuickItem;

// Test hook QTCRAFT_LAYOUT_CHECK=1 (screenshot runs): finds the shown items that reach outside the
// window, such as a button pushed below the bottom edge by a column taller than the window. An item
// that clips its children (a list, a scroll view) is checked itself, its content is not: content
// outside a clipping item is scrolled away, not lost.
class QcLayoutCheck
{
public:
    // One line per shown item under `root` (root itself excluded) whose scene rectangle reaches
    // more than half a pixel outside `bounds`: type, object name, text and rectangle.
    static QStringList itemsOutside(const QQuickItem &root, const QRectF &bounds);
    // One line per shown status row of the HUD (the item named "hud", its property `statusRows`)
    // that the shown chat lines (the item named "hudChat") overlap; empty without a shown HUD or
    // without chat lines.
    static QStringList chatOverlaps(const QQuickItem &root);
    // Which parts of the HUD show (decision E-927), as "chat 1 hint 0 target 1 crosshair 1 hotbar 1
    // hearts 1": the chat lines ("hudChat"), the world hint ("hudHint"), the name of the block in
    // view ("hudTarget"), the crosshair ("hudCrosshair"), the hotbar ("hotbar") and the hearts
    // ("hearts"); 0 for a part that is missing or hidden (by itself or with the HUD).
    static QString hudPartsShown(const QQuickItem &root);
    // One line per pair of advancements (items named "advancement_<name>") in the shown tree (the
    // item named "advancementTree", decision E-924) whose rectangles overlap; empty without it.
    static QStringList advancementOverlaps(const QQuickItem &root);

private:
    // `item` and the items below it that reach outside `bounds` (clipping items hide what is below them).
    [[nodiscard]] static QStringList outside(const QQuickItem &item, const QRectF &bounds);
    static QList<const QQuickItem *> statusRows(const QQuickItem &hud);
    static const QQuickItem *findItem(const QQuickItem &root, const QString &name);
    // The shown items below `root` whose object name starts with `prefix`, in tree order.
    static QList<const QQuickItem *> itemsNamed(const QQuickItem &root, const QString &prefix);
    static bool shown(const QQuickItem &item);
    static QString describe(const QQuickItem &item, const QRectF &rect);
};
