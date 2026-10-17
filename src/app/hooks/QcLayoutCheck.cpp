#include "app/hooks/QcLayoutCheck.h"

#include <QQuickItem>

using namespace Qt::StringLiterals;

namespace {
constexpr qreal tolerance = 0.5;
}

QStringList QcLayoutCheck::itemsOutside(const QQuickItem &root, const QRectF &bounds)
{
    QStringList found;
    const QList<QQuickItem *> children = root.childItems();
    for (const QQuickItem *child : children) {
        collect(*child, bounds, found);
    }
    return found;
}

void QcLayoutCheck::collect(const QQuickItem &item, const QRectF &bounds, QStringList &found)
{
    if (!item.isVisible() || item.opacity() <= 0.0) {
        return;
    }

    // Empty items (anchors, spacers, positioners without content) show nothing themselves.
    if (item.width() > 0.0 && item.height() > 0.0) {
        const QRectF rect = item.mapRectToScene(QRectF(0.0, 0.0, item.width(), item.height()));
        if (!bounds.adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(rect)) {
            found.append(describe(item, rect));
        }
    }

    if (item.clip()) {
        return;
    }
    const QList<QQuickItem *> children = item.childItems();
    for (const QQuickItem *child : children) {
        collect(*child, bounds, found);
    }
}

QString QcLayoutCheck::describe(const QQuickItem &item, const QRectF &rect)
{
    const QString text = item.property("text").toString();
    return u"%1 \"%2\" \"%3\" at %4,%5 size %6x%7"_s.arg(QString::fromLatin1(item.metaObject()->className()), item.objectName(), text)
        .arg(rect.x())
        .arg(rect.y())
        .arg(rect.width())
        .arg(rect.height());
}
