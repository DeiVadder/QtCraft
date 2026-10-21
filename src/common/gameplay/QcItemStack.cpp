#include "common/gameplay/QcItemStack.h"

#include "common/core/QcLogging.h"

#include <algorithm>
#include <utility>

QcItemStack QcItemStack::withData(const QcItemData &next) const
{
    QcItemStack copy = *this;
    QcItemData normal = next.normalized();
    if (!normal.fitsLimits()) {
        qCWarning(lcItemData) << "Refused item data beyond its limits for item" << item << ": name" << normal.name.size() << "characters,"
                           << normal.contents.size() << "slots," << normal.depth() << "levels," << normal.stacksInside() << "stacks inside";
        return copy;
    }
    copy.data.reset(normal.isEmpty() ? nullptr : new QcSharedItemData(std::move(normal)));
    return copy;
}

QcItemStack QcItemStack::withMapId(quint32 id) const
{
    QcItemData next = itemData();
    next.mapId = id;
    return withData(next);
}

QcItemStack QcItemStack::withContents(QList<QcItemStack> stacks) const
{
    QcItemData next = itemData();
    next.contents = std::move(stacks);
    return withData(next);
}

QcItemStack QcItemStack::withName(QString text) const
{
    QcItemData next = itemData();
    next.name = std::move(text);
    return withData(next);
}

QcItemStack QcItemStack::withoutKept() const
{
    if (itemData().kept.isEmpty()) {
        return *this;
    }
    QcItemData known = itemData();
    known.kept = {};
    return withData(known);
}

QList<QcItemStack> QcItemStack::split(int maxStack) const
{
    QList<QcItemStack> parts;
    if (isEmpty()) {
        return parts;
    }

    const int limit = std::max(1, stackLimit(maxStack));
    int left = hasContents() ? 1 : count;
    QcItemStack part = *this;
    while (left > 0) {
        part.count = std::min(left, limit);
        left -= part.count;
        parts.append(part);
        part = part.withoutKept();
    }
    return parts;
}

QcItemData QcItemData::normalized() const
{
    QcItemData normal = *this;
    while (!normal.contents.isEmpty() && normal.contents.constLast().isEmpty()) {
        normal.contents.removeLast();
    }
    // An empty slot is one kind of empty stack, so equal contents compare equal.
    for (QcItemStack &slot : normal.contents) {
        if (slot.isEmpty()) {
            slot = {};
        }
    }
    return normal;
}

int QcItemData::depth() const
{
    int deepest = 0;
    for (const QcItemStack &slot : contents) {
        deepest = std::max(deepest, slot.itemData().depth());
    }
    return contents.isEmpty() ? 0 : deepest + 1;
}

int QcItemData::stacksInside() const
{
    int stacks = int(contents.size());
    for (const QcItemStack &slot : contents) {
        stacks += slot.itemData().stacksInside();
    }
    return stacks;
}

bool QcItemData::fitsLimits() const
{
    // The stacks inside got their data through QcItemStack::withData, which checked it.
    return name.size() <= maxNameLength && contents.size() <= maxContents && depth() <= maxDepth && stacksInside() <= maxStacksInside;
}

bool QcItemData::operator==(const QcItemData &other) const
{
    return mapId == other.mapId && name == other.name && contents == other.contents && kept == other.kept;
}
