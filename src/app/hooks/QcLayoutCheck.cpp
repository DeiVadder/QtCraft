#include "app/hooks/QcLayoutCheck.h"

#include <QQmlListReference>
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
        found += outside(*child, bounds);
    }
    return found;
}

QStringList QcLayoutCheck::outside(const QQuickItem &item, const QRectF &bounds)
{
    QStringList found;
    if (!item.isVisible() || item.opacity() <= 0.0) {
        return found;
    }

    // Empty items (anchors, spacers, positioners without content) show nothing themselves.
    if (item.width() > 0.0 && item.height() > 0.0) {
        const QRectF rect = item.mapRectToScene(QRectF(0.0, 0.0, item.width(), item.height()));
        if (!bounds.adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(rect)) {
            found.append(describe(item, rect));
        }
    }

    if (item.clip()) {
        return found;
    }
    const QList<QQuickItem *> children = item.childItems();
    for (const QQuickItem *child : children) {
        found += outside(*child, bounds);
    }
    return found;
}

QStringList QcLayoutCheck::chatOverlaps(const QQuickItem &root)
{
    const QQuickItem *hud = findItem(root, u"hud"_s);
    const QQuickItem *chat = findItem(root, u"hudChat"_s);
    if (!hud || !chat || !shown(*hud) || !shown(*chat) || chat->height() <= 0.0) {
        return {};
    }

    const QRectF chatRect = chat->mapRectToScene(QRectF(0.0, 0.0, chat->width(), chat->height()));
    QStringList found;
    const QList<const QQuickItem *> rows = statusRows(*hud);
    for (const QQuickItem *row : rows) {
        if (!shown(*row) || row->width() <= 0.0 || row->height() <= 0.0) {
            continue;
        }

        const QRectF rect = row->mapRectToScene(QRectF(0.0, 0.0, row->width(), row->height()));
        if (chatRect.adjusted(tolerance, tolerance, -tolerance, -tolerance).intersects(rect)) {
            found.append(describe(*row, rect));
        }
    }
    return found;
}

QList<const QQuickItem *> QcLayoutCheck::statusRows(const QQuickItem &hud)
{
    QList<const QQuickItem *> rows;
    const QQmlListReference list(hud.property("statusRows"));
    for (qsizetype index = 0; index < list.count(); ++index) {
        if (const auto *item = qobject_cast<const QQuickItem *>(list.at(index))) {
            rows.append(item);
        }
    }
    return rows;
}

const QQuickItem *QcLayoutCheck::findItem(const QQuickItem &root, const QString &name)
{
    const QList<QQuickItem *> children = root.childItems();
    for (const QQuickItem *child : children) {
        if (child->objectName() == name) {
            return child;
        }
        if (const QQuickItem *found = findItem(*child, name)) {
            return found;
        }
    }
    return nullptr;
}

bool QcLayoutCheck::shown(const QQuickItem &item)
{
    for (const QQuickItem *at = &item; at; at = at->parentItem()) {
        if (!at->isVisible() || at->opacity() <= 0.0) {
            return false;
        }
    }
    return true;
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
