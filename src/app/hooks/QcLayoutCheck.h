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

private:
    // `item` and the items below it that reach outside `bounds` (clipping items hide what is below them).
    [[nodiscard]] static QStringList outside(const QQuickItem &item, const QRectF &bounds);
    static QList<const QQuickItem *> statusRows(const QQuickItem &hud);
    static const QQuickItem *findItem(const QQuickItem &root, const QString &name);
    static bool shown(const QQuickItem &item);
    static QString describe(const QQuickItem &item, const QRectF &rect);
};
