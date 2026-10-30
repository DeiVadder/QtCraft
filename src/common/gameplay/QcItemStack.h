// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/items/QcItemDefinition.h"
#include "common/core/QcKeptComponent.h"

#include <QExplicitlySharedDataPointer>
#include <QList>
#include <QSharedData>
#include <QString>

#include <optional>
#include <utility>

class QcItemData;
struct QcSharedItemData;

/*! \brief An enchantment an item carries (decision E-864): the enchantment's content name and
    its level. */
struct QcEnchantment
{
    /*! The enchantment's content name. */
    QString name;
    /*! Its level. */
    int level = 1;

    bool operator==(const QcEnchantment &) const = default;
};

/*! \brief A number of identical items, e.g. one inventory slot.

    \c {damage} counts tool uses. What only some stacks carry (a map id, contents, a custom name)
    is in \c {data} (decision E-823): null for plain stacks, so they pay one null pointer (a
    stack is 24 bytes); shared and never changed in place, so copying a stack copies a pointer.
    Set it only through the with…() functions, which leave it null when empty. */
struct QcItemStack
{
    /*! The item; qcNoItem for an empty stack. */
    QcItemId item = qcNoItem;
    /*! How many items. */
    int count = 0;
    /*! Uses the items have worn off. */
    int damage = 0;
    /*! The optional data (see the struct). */
    QExplicitlySharedDataPointer<const QcSharedItemData> data = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisation warning-free

    /*! Returns whether the stack holds no item (no item or a count of 0 or less). */
    bool isEmpty() const { return item == qcNoItem || count <= 0; }
    /*! Returns whether the items of this stack and \a other are the same and may go into one
        stack (up to stackLimit): the same item, damage and data (decisions E-798, E-823). Stacks
        with contents never stack, so each holds one item (stackLimit). Items that wear out stack
        to one, so worn tools never merge. */
    bool stacksWith(const QcItemStack &other) const;
    /*! Returns the stack size this stack may reach given its item's \a maxStack: 1 with
        contents. */
    int stackLimit(int maxStack) const { return hasContents() ? 1 : maxStack; }

    /*! Returns the same items and data, \a amount of them. */
    [[nodiscard]] QcItemStack withCount(int amount) const;
    /*! Returns the same stack with \a next as its data (null when \a next holds nothing). Data
        beyond the limits of QcItemData (QcItemData::fitsLimits) is refused with a warning: the
        stack keeps its data, so the server never makes a stack that its readers would refuse. */
    [[nodiscard]] QcItemStack withData(const QcItemData &next) const;
    /*! Returns the same stack with the map id \a id (withData). */
    [[nodiscard]] QcItemStack withMapId(quint32 id) const;
    /*! Returns the same stack holding \a stacks (withData). */
    [[nodiscard]] QcItemStack withContents(QList<QcItemStack> stacks) const;
    /*! Returns the same stack with the custom name \a text (withData). */
    [[nodiscard]] QcItemStack withName(QString text) const;
    /*! Returns the same stack with the enchantments \a list (withData). */
    [[nodiscard]] QcItemStack withEnchantments(QList<QcEnchantment> list) const;
    /*! Returns the same stack with \a uses anvil uses, clamped to 0..QcItemData::maxPriorWork
        (withData). */
    [[nodiscard]] QcItemStack withPriorWork(int uses) const;
    /*! Returns the same stack with \a next as its data, or nothing (with the warning of
        withData) when the data is beyond the limits. Callers that take items out of slots to put
        them into the data (filling a backpack) ask first, so a refusal loses and doubles nothing
        (stage 68.2). */
    [[nodiscard]] std::optional<QcItemStack> tryWithData(const QcItemData &next) const;
    /*! Returns the same stack holding \a stacks, or nothing when the data would be beyond the
        limits (tryWithData). */
    [[nodiscard]] std::optional<QcItemStack> tryWithContents(QList<QcItemStack> stacks) const;
    /*! Returns the same stack without the components a save kept for a newer version
        (QcItemData::kept). */
    [[nodiscard]] QcItemStack withoutKept() const;
    /*! What clients need to draw the stack lying on the ground or in a frame (decision E-832):
        item, count, damage, map id and name, without contents and kept components, so a dropped
        backpack does not send what it holds to everyone near. */
    [[nodiscard]] QcItemStack forDisplay() const;
    /*! Returns the order of \a a and \a b (negative, 0 or positive) in a total order of stacks
        (empty ones first) by item, damage, count and data, so that rules which go through stacks
        in a fixed order (hoppers sharing a target, decision E-344) never depend on directions. */
    [[nodiscard]] static int compare(const QcItemStack &a, const QcItemStack &b);
    /*! Returns the stack in parts of at most stackLimit(\a maxStack) items, for a stack larger than
        its item allows (a generous block drop, a save from before a mod lowered \a maxStack). Only
        the first part keeps the kept components: one may hold items, which a copy would
        multiply. A stack with contents is one item, whatever its count, for the same reason.
        Empty for an empty stack. */
    [[nodiscard]] QList<QcItemStack> split(int maxStack) const;

    /*! Returns the map id of a filled map, 0 for none (decision E-798). */
    quint32 mapId() const;
    /*! Returns the stacks held inside (a backpack's slots, stage 68.2); empty for none. */
    const QList<QcItemStack> &contents() const;
    /*! Returns whether it holds stacks inside. */
    bool hasContents() const;
    /*! A custom name (stage 41.2a); empty for none. */
    QString name() const;
    /*! Returns the enchantments it carries (decision E-864); empty for none. */
    const QList<QcEnchantment> &enchantments() const;
    /*! How often it went through an anvil (decision E-866). */
    int priorWork() const;
    /*! Returns the data, or an empty one. */
    const QcItemData &itemData() const;

    /*! Returns whether \a other has the same item, count, damage and data. */
    bool operator==(const QcItemStack &other) const;
};

/*! \brief The optional data of an item stack (decision E-823): typed components, each with a fixed
    id that is never reused (ids from QcItemData::firstModId on belong to mods).

    Saves keep components this version does not know (\c {kept}, QcComponentFrames); the wire
    carries only the known ones. */
class QcItemData
{
public:
    /*! The ids of the known components. */
    enum class Component : quint16 {
        /*! The map id of a filled map (mapId). */
        MapId = 1,
        /*! The stacks held inside (contents). */
        Contents = 2,
        /*! The custom name (name). */
        Name = 3,
        /*! The enchantments (enchantments). */
        Enchantments = 4,
        /*! The anvil uses (priorWork). */
        PriorWork = 5,
    };
    /*! The first component id that belongs to mods. */
    static constexpr quint16 firstModId = 0x8000;
    /*! The longest custom name (characters). */
    static constexpr qsizetype maxNameLength = 50;
    /*! The most stacks a contents list holds (slots, empty ones included). */
    static constexpr qsizetype maxContents = 54;
    /*! Contents nest at most this deep (a stack in a stack in a stack); deeper is damaged data. */
    static constexpr int maxDepth = 3;
    /*! The most stacks inside one stack on all levels together (empty slots included). With the
        other limits a stack without contents takes at most about 2.2 KB on the wire (item, count
        and damage 12 bytes, map id 4, a name of maxNameLength characters 104, maxEnchantments
        enchantments of maxEnchantmentName characters 2128, and the component bits, counts and
        prior work), so the largest stack, with maxStacksInside stacks inside, takes about 146 KB.
        These limits alone keep neither a list of QcProtocolCodec::readStacks' 1024 such stacks
        (about 150 MB) nor a player's inventory with its 50 stacks (about 7 MB) below a frame
        (QcProtocol::maxFrameBytes); stacks from clients carry no contents
        (QcProtocolCodec::readFlatStack). */
    static constexpr int maxStacksInside = 64;
    /*! The most enchantments on one stack, the longest enchantment name and the highest level.
        \qcshared */
    static constexpr qsizetype maxEnchantments = 16;
    static constexpr qsizetype maxEnchantmentName = 64;
    static constexpr int maxEnchantmentLevel = 255;
    /*! The most anvil uses a stack remembers (decision E-866); its penalty is 2^priorWork − 1
        levels. */
    static constexpr int maxPriorWork = 31;

    /*! The map id of a filled map; 0: none. */
    quint32 mapId = 0;
    /*! The stacks held inside, by slot; trailing empty slots are left out, so all empty is none. */
    QList<QcItemStack> contents;
    /*! The custom name; empty for none. */
    QString name;
    /*! The enchantments; empty for none. */
    QList<QcEnchantment> enchantments;
    /*! How often the stack went through an anvil (decision E-866); 0 for never. */
    int priorWork = 0;
    /*! Components read from a save this version does not know, written back as read. */
    QcKeptComponents kept;

    /*! Returns whether it holds no component. */
    bool isEmpty() const { return mapId == 0 && contents.isEmpty() && name.isEmpty() && enchantments.isEmpty() && priorWork == 0 && kept.isEmpty(); }
    /*! Returns the same data with trailing empty slots of the contents left out. */
    [[nodiscard]] QcItemData normalized() const;
    /*! How deep its contents nest: 0 without contents, 1 for contents without contents, …. */
    [[nodiscard]] int depth() const;
    /*! How many stacks its contents hold on all levels together, empty slots included. */
    [[nodiscard]] int stacksInside() const;
    /*! Returns whether it keeps the limits every reader checks: a name of at most maxNameLength
        characters, at most maxContents slots, maxDepth levels and maxStacksInside stacks (the
        stacks inside were checked when they got their data). */
    [[nodiscard]] bool fitsLimits() const;
    /*! Returns the order of \a a and \a b in a total order of data (QcItemStack::compare):
        negative, 0 or positive. */
    [[nodiscard]] static int compare(const QcItemData &a, const QcItemData &b);
    /*! Returns whether \a other holds the same components. */
    bool operator==(const QcItemData &other) const;
};

/*! \brief The shared, reference-counted holder of a stack's data. */
struct QcSharedItemData : QSharedData
{
    explicit QcSharedItemData(QcItemData held)
        : value(std::move(held))
    {
    }

    /*! The data. */
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

inline int QcItemStack::priorWork() const
{
    return data ? data->value.priorWork : 0;
}

inline const QList<QcEnchantment> &QcItemStack::enchantments() const
{
    return itemData().enchantments;
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
