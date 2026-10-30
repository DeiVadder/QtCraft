// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcVec3.h"

#include <QString>

#include <array>
#include <optional>

// How an item is held (decision E-831): where it sits in the first-person fist and in a figure's
// fist. Content sets it per category (QcHoldDefaults, `held.json`) and per item or block (`hold`);
// every field a definition leaves out comes from the level below, so a mod moves one item by
// setting one field.

/*! \brief Where a hand takes a sprite: by the end of its handle at the lower left (tools, weapons),
    in the middle (launchers) or from below (anything else).

    Blocks are held by the middle of their boxes. */
enum class QcHoldGrip : quint8 {
    Base, // From below, data name "base".
    Handle, // By the end of the handle at the lower left, data name "handle".
    Middle, // In the middle, data name "middle".
};

/*! Number of values of QcHoldGrip. */
inline constexpr std::size_t qcHoldGripCount = 3;

/*! \brief What kind of thing is held; each kind has its defaults.

    A definition picks its kind from what it is (the client's look of the item: block, launcher,
    tool, weapon) or names one in \c {hold.category}. */
enum class QcHoldCategory : quint8 {
    Tool, // An item with a tool category or a fishing rod, data name "tool".
    Weapon, // Any other sprite item dealing more than 1 damage, data name "weapon".
    Launcher, // An item with a launcher (bow), data name "launcher".
    Sprite, // Any other flat item, data name "sprite".
    Block, // An item drawn as a full block, data name "block".
    PartialBlock, // An item drawn as a block of smaller boxes, data name "partialBlock".
};

/*! Number of values of QcHoldCategory. */
inline constexpr std::size_t qcHoldCategoryCount = 6;

/*! \brief One placement of the held thing around the grip.

    A point of the item (a sprite one unit across, a block one unit per block edge, both relative
    to the grip) is scaled by \c {scale} (metres per unit), turned by \c {rotate} (degrees about
    Y, then X, then Z) and moved by \c {translate} (metres) in the frame of the fist: for the
    first person the camera's (x right, y up, z backwards), for a figure its arm's model space
    (the arm hanging down, the figure facing -Z). Unset fields take the level below. */
struct QcHoldTransform
{
    /*! Offset in metres (\c {"translate"}); unset takes the level below. */
    std::optional<QcVec3> translate;
    /*! Angles in degrees about Y, then X, then Z (\c {"rotate"}); unset takes the level below. */
    std::optional<QcVec3> rotate;
    /*! Metres per unit on each axis (\c {"scale"}); unset takes the level below. */
    std::optional<QcVec3> scale;

    /*! Returns this transform with every unset field taken from \a base. */
    [[nodiscard]] QcHoldTransform over(const QcHoldTransform &base) const;
    /*! Returns whether no field is set. */
    bool isEmpty() const { return !translate && !rotate && !scale; }
    bool operator==(const QcHoldTransform &) const = default;
};

/*! \brief The \c {hold} of an item or block definition, or one category's defaults.

    \c {swing} is where the item sits in the first-person fist in the middle of a swing
    (\c {translate} and \c {rotate} only, no \c {scale}): the hand blends from \c {firstPerson}
    to it and back over the swing, so a tool can rest with its flat side towards the viewer and
    still strike with its point. Its unset fields keep the first-person values (no change during
    the swing). */
struct QcHoldDisplay
{
    /*! Category whose defaults apply (\c {"category"}, definitions only); unset picks it from
        what the item is. */
    std::optional<QcHoldCategory> category;
    /*! Where the hand takes the item (\c {"grip"}); unset takes the level below. */
    std::optional<QcHoldGrip> grip;
    /*! Placement in the first-person fist (\c {"firstPerson"}). */
    QcHoldTransform firstPerson;
    /*! Placement in a figure's fist (\c {"thirdPerson"}). */
    QcHoldTransform thirdPerson;
    /*! First-person placement in the middle of a swing (\c {"swing"}, no scale). */
    QcHoldTransform swing;

    /*! Returns this display with every unset field taken from \a base. */
    [[nodiscard]] QcHoldDisplay over(const QcHoldDisplay &base) const;
    /*! Returns whether no field and no transform field is set. */
    bool isEmpty() const { return !category && !grip && firstPerson.isEmpty() && thirdPerson.isEmpty() && swing.isEmpty(); }
    bool operator==(const QcHoldDisplay &) const = default;
};

/*! \brief The defaults of every category (\c {held.json}); a later pack's file overrides the
    fields it sets. */
struct QcHoldDefaults
{
    /*! The defaults of each category, indexed by QcHoldCategory. */
    std::array<QcHoldDisplay, qcHoldCategoryCount> categories;

    /*! Returns the defaults of \a category. */
    const QcHoldDisplay &of(QcHoldCategory category) const { return categories[static_cast<std::size_t>(category)]; }
    /*! Returns these defaults with every field \a later sets taken from it. */
    [[nodiscard]] QcHoldDefaults merged(const QcHoldDefaults &later) const;
    bool operator==(const QcHoldDefaults &) const = default;
};

/*! \brief The JSON names of hold categories and grips ("tool", "partialBlock", "handle", ...). */
namespace QcHoldNames {

/*! Returns the JSON name of \a category. */
QString category(QcHoldCategory category);
/*! Returns the category whose JSON name is \a name, or std::nullopt for an unknown name. */
std::optional<QcHoldCategory> category(const QString &name);
/*! Returns the JSON name of \a grip. */
QString grip(QcHoldGrip grip);
/*! Returns the grip whose JSON name is \a name, or std::nullopt for an unknown name. */
std::optional<QcHoldGrip> grip(const QString &name);

} // namespace QcHoldNames
