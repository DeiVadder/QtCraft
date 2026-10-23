#include "common/gameplay/QcItemStack.h"

#include "common/core/QcLogging.h"

#include <algorithm>
#include <tuple>
#include <utility>

namespace {

// Kept components in a fixed order: by count, then id, version, tail and bytes.
int compareKept(const QcKeptComponents &a, const QcKeptComponents &b)
{
    if (a.size() != b.size()) {
        return a.size() < b.size() ? -1 : 1;
    }
    for (qsizetype i = 0; i < a.size(); ++i) {
        const auto key = [](const QcKeptComponent &kept) { return std::tuple{kept.id, kept.version, kept.tail}; };
        if (key(a[i]) != key(b[i])) {
            return key(a[i]) < key(b[i]) ? -1 : 1;
        }
        if (const int bytes = a[i].payload.compare(b[i].payload); bytes != 0) {
            return bytes;
        }
    }
    return 0;
}

} // namespace

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

std::optional<QcItemStack> QcItemStack::tryWithData(const QcItemData &next) const
{
    if (!next.normalized().fitsLimits()) {
        // The same refusal and warning as withData gives.
        [[maybe_unused]] const QcItemStack unchanged = withData(next);
        return std::nullopt;
    }
    return withData(next);
}

std::optional<QcItemStack> QcItemStack::tryWithContents(QList<QcItemStack> stacks) const
{
    QcItemData next = itemData();
    next.contents = std::move(stacks);
    return tryWithData(next);
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

QcItemStack QcItemStack::withEnchantments(QList<QcEnchantment> list) const
{
    QcItemData next = itemData();
    next.enchantments = std::move(list);
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

QcItemStack QcItemStack::forDisplay() const
{
    if (!data || (itemData().contents.isEmpty() && itemData().kept.isEmpty())) {
        return *this;
    }
    QcItemData shown;
    shown.mapId = mapId();
    shown.name = name();
    shown.enchantments = enchantments();
    return withData(shown);
}

int QcItemStack::compare(const QcItemStack &a, const QcItemStack &b)
{
    const bool aEmpty = a.isEmpty();
    const bool bEmpty = b.isEmpty();
    if (aEmpty || bEmpty) {
        return int(bEmpty) - int(aEmpty);
    }
    if (a.item != b.item) {
        return a.item < b.item ? -1 : 1;
    }
    if (a.damage != b.damage) {
        return a.damage < b.damage ? -1 : 1;
    }
    if (a.count != b.count) {
        return a.count < b.count ? -1 : 1;
    }
    return a.data == b.data ? 0 : QcItemData::compare(a.itemData(), b.itemData());
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
    const auto validEnchantment = [](const QcEnchantment &enchantment) {
        return !enchantment.name.isEmpty() && enchantment.name.size() <= maxEnchantmentName && enchantment.level >= 1
               && enchantment.level <= maxEnchantmentLevel;
    };
    return name.size() <= maxNameLength && contents.size() <= maxContents && depth() <= maxDepth && stacksInside() <= maxStacksInside
           && enchantments.size() <= maxEnchantments && std::ranges::all_of(enchantments, validEnchantment);
}

int QcItemData::compare(const QcItemData &a, const QcItemData &b)
{
    if (a.mapId != b.mapId) {
        return a.mapId < b.mapId ? -1 : 1;
    }
    if (const int names = a.name.compare(b.name); names != 0) {
        return names;
    }
    if (a.enchantments.size() != b.enchantments.size()) {
        return a.enchantments.size() < b.enchantments.size() ? -1 : 1;
    }
    for (qsizetype i = 0; i < a.enchantments.size(); ++i) {
        const auto key = [](const QcEnchantment &enchantment) { return std::tuple{enchantment.name, enchantment.level}; };
        if (key(a.enchantments[i]) != key(b.enchantments[i])) {
            return key(a.enchantments[i]) < key(b.enchantments[i]) ? -1 : 1;
        }
    }
    if (a.contents.size() != b.contents.size()) {
        return a.contents.size() < b.contents.size() ? -1 : 1;
    }
    for (qsizetype i = 0; i < a.contents.size(); ++i) {
        if (const int slot = QcItemStack::compare(a.contents[i], b.contents[i]); slot != 0) {
            return slot;
        }
    }
    return compareKept(a.kept, b.kept);
}

bool QcItemData::operator==(const QcItemData &other) const
{
    return mapId == other.mapId && name == other.name && enchantments == other.enchantments && contents == other.contents && kept == other.kept;
}
