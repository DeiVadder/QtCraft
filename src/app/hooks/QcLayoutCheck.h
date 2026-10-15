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

private:
    static void collect(const QQuickItem &item, const QRectF &bounds, QStringList &found);
    static QString describe(const QQuickItem &item, const QRectF &rect);
};
