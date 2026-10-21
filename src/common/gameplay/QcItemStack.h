#pragma once

#include "common/content/definitions/QcItemDefinition.h"
#include "common/core/QcKeptComponent.h"

#include <QExplicitlySharedDataPointer>
#include <QList>
#include <QSharedData>
#include <QString>

#include <utility>

class QcItemData;
struct QcSharedItemData;

// A number of identical items, e.g. one inventory slot. `damage` counts tool uses. What only some
// stacks carry (a map id, contents, a custom name) is in `data` (decision E-823): null for plain
// stacks, so they pay one null pointer (a stack is 24 bytes); shared and never changed in place, so
// copying a stack copies a pointer. Set it only through the with…() functions, which leave it null when empty.
struct QcItemStack
{
    QcItemId item = qcNoItem;
    int count = 0;
    int damage = 0;
    QExplicitlySharedDataPointer<const QcSharedItemData> data = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisation warning-free

    bool isEmpty() const { return item == qcNoItem || count <= 0; }
    // Whether the items of both stacks are the same and may go into one stack (up to stackLimit):
    // the same item, damage and data (decisions E-798, E-823). Stacks with contents never stack, so
    // each holds one item (stackLimit). Items that wear out stack to one, so worn tools never merge.
    bool stacksWith(const QcItemStack &other) const;
    // The stack size this stack may reach given its item's maxStack: 1 with contents.
    int stackLimit(int maxStack) const { return hasContents() ? 1 : maxStack; }

    // The same items and data, `amount` of them.
    [[nodiscard]] QcItemStack withCount(int amount) const;
    // The same stack with `next` as its data (null when `next` holds nothing). Data beyond the
    // limits of QcItemData (QcItemData::fitsLimits) is refused with a warning: the stack keeps its
    // data, so the server never makes a stack that its readers would refuse.
    [[nodiscard]] QcItemStack withData(const QcItemData &next) const;
    [[nodiscard]] QcItemStack withMapId(quint32 id) const;
    [[nodiscard]] QcItemStack withContents(QList<QcItemStack> stacks) const;
    [[nodiscard]] QcItemStack withName(QString text) const;
    // The same stack without the components a save kept for a newer version (QcItemData::kept).
    [[nodiscard]] QcItemStack withoutKept() const;
    // The stack in parts of at most stackLimit(maxStack) items, for a stack larger than its item
    // allows (a generous block drop, a save from before a mod lowered maxStack). Only the first part
    // keeps the kept components: one may hold items, which a copy would multiply. A stack with
    // contents is one item, whatever its count, for the same reason. Empty for an empty stack.
    [[nodiscard]] QList<QcItemStack> split(int maxStack) const;

    // The map id of a filled map, 0 for none (decision E-798).
    quint32 mapId() const;
    // The stacks held inside (a backpack's slots, stage 68.2); empty for none.
    const QList<QcItemStack> &contents() const;
    bool hasContents() const;
    // A custom name (stage 41.2a); empty for none.
    QString name() const;
    // The data, or an empty one.
    const QcItemData &itemData() const;

    bool operator==(const QcItemStack &other) const;
};

// The optional data of an item stack (decision E-823): typed components, each with a fixed id that
// is never reused (ids from QcItemData::firstModId on belong to mods). Saves keep components this
// version does not know (`kept`, QcComponentFrames); the wire carries only the known ones.
class QcItemData
{
public:
    enum class Component : quint16 {
        MapId = 1,
        Contents = 2,
        Name = 3,
    };
    static constexpr quint16 firstModId = 0x8000;
    // The longest custom name (characters).
    static constexpr qsizetype maxNameLength = 50;
    // The most stacks a contents list holds (slots, empty ones included).
    static constexpr qsizetype maxContents = 54;
    // Contents nest at most this deep (a stack in a stack in a stack); deeper is damaged data.
    static constexpr int maxDepth = 3;
    // The most stacks inside one stack on all levels together (empty slots included). With it the
    // largest stack is about 8 KB on the wire, so even QcProtocolCodec::readStacks' 1024 such
    // stacks stay far below a frame (QcProtocol::maxFrameBytes), and a player's inventory below 1 MiB.
    static constexpr int maxStacksInside = 64;

    // The map id of a filled map; 0: none.
    quint32 mapId = 0;
    // The stacks held inside, by slot; trailing empty slots are left out, so all empty is none.
    QList<QcItemStack> contents;
    QString name;
    // Components read from a save this version does not know, written back as read.
    QcKeptComponents kept;

    bool isEmpty() const { return mapId == 0 && contents.isEmpty() && name.isEmpty() && kept.isEmpty(); }
    // The same data with trailing empty slots of the contents left out.
    [[nodiscard]] QcItemData normalized() const;
    // How deep its contents nest: 0 without contents, 1 for contents without contents, ….
    [[nodiscard]] int depth() const;
    // How many stacks its contents hold on all levels together, empty slots included.
    [[nodiscard]] int stacksInside() const;
    // Whether it keeps the limits every reader checks: a name of at most maxNameLength characters,
    // at most maxContents slots, maxDepth levels and maxStacksInside stacks (the stacks inside
    // were checked when they got their data).
    [[nodiscard]] bool fitsLimits() const;
    bool operator==(const QcItemData &other) const;
};

// The shared, reference-counted holder of a stack's data.
struct QcSharedItemData : QSharedData
{
    explicit QcSharedItemData(QcItemData held)
        : value(std::move(held))
    {
    }

    QcItemData value;
};

inline const QcItemData &QcItemStack::itemData() const
{
    static const QcItemData none;
    return data ? data->value : none;
}

inline quint32 QcItemStack::mapId() const
{
    return data ? data->value.mapId : 0;
}

inline const QList<QcItemStack> &QcItemStack::contents() const
{
    return itemData().contents;
}

inline bool QcItemStack::hasContents() const
{
    return data && !data->value.contents.isEmpty();
}

inline QString QcItemStack::name() const
{
    return data ? data->value.name : QString();
}

inline bool QcItemStack::stacksWith(const QcItemStack &other) const
{
    if (item != other.item || damage != other.damage || hasContents() || other.hasContents()) {
        return false;
    }
    return data == other.data || itemData() == other.itemData();
}

inline QcItemStack QcItemStack::withCount(int amount) const
{
    QcItemStack copy = *this;
    copy.count = amount;
    return copy;
}

inline bool QcItemStack::operator==(const QcItemStack &other) const
{
    if (item != other.item || count != other.count || damage != other.damage) {
        return false;
    }
    return data == other.data || itemData() == other.itemData();
}
