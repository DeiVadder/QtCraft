#pragma once

#include "common/core/QcVec3.h"

#include <QString>

#include <array>
#include <optional>

// How an item is held (decision E-831): where it sits in the first-person fist and in a figure's
// fist. Content sets it per category (QcHoldDefaults, `held.json`) and per item or block (`hold`);
// every field a definition leaves out comes from the level below, so a mod moves one item by
// setting one field.

// Where a hand takes a sprite: by the end of its handle at the lower left (tools, weapons), in the
// middle (launchers) or from below (anything else). Blocks are held by the middle of their boxes.
enum class QcHoldGrip : quint8 {
    Base,
    Handle,
    Middle,
};

inline constexpr std::size_t qcHoldGripCount = 3;

// What kind of thing is held; each kind has its defaults. A definition picks its kind from what
// it is (the client's look of the item: block, launcher, tool, weapon) or names one in
// `hold.category`.
enum class QcHoldCategory : quint8 {
    Tool,
    Weapon,
    Launcher,
    Sprite,
    Block,
    PartialBlock,
};

inline constexpr std::size_t qcHoldCategoryCount = 6;

// One placement of the held thing around the grip. A point of the item (a sprite one unit across,
// a block one unit per block edge, both relative to the grip) is scaled by `scale` (metres per
// unit), turned by `rotate` (degrees about Y, then X, then Z) and moved by `translate` (metres) in
// the frame of the fist: for the first person the camera's (x right, y up, z backwards), for a
// figure its arm's model space (the arm hanging down, the figure facing -Z). Unset fields take the
// level below.
struct QcHoldTransform
{
    std::optional<QcVec3> translate;
    std::optional<QcVec3> rotate;
    std::optional<QcVec3> scale;

    // This transform with every unset field taken from `base`.
    [[nodiscard]] QcHoldTransform over(const QcHoldTransform &base) const;
    bool isEmpty() const { return !translate && !rotate && !scale; }
    bool operator==(const QcHoldTransform &) const = default;
};

// The `hold` of an item or block definition, or one category's defaults. `swing` is where the
// item sits in the first-person fist in the middle of a swing (`translate` and `rotate` only, no
// `scale`): the hand blends from `firstPerson` to it and back over the swing, so a tool can rest
// with its flat side towards the viewer and still strike with its point. Its unset fields keep the
// first-person values (no change during the swing).
struct QcHoldDisplay
{
    std::optional<QcHoldCategory> category;
    std::optional<QcHoldGrip> grip;
    QcHoldTransform firstPerson;
    QcHoldTransform thirdPerson;
    QcHoldTransform swing;

    // This display with every unset field taken from `base`.
    [[nodiscard]] QcHoldDisplay over(const QcHoldDisplay &base) const;
    bool isEmpty() const { return !category && !grip && firstPerson.isEmpty() && thirdPerson.isEmpty() && swing.isEmpty(); }
    bool operator==(const QcHoldDisplay &) const = default;
};

// The defaults of every category (`held.json`); a later pack's file overrides the fields it sets.
struct QcHoldDefaults
{
    std::array<QcHoldDisplay, qcHoldCategoryCount> categories;

    const QcHoldDisplay &of(QcHoldCategory category) const { return categories[static_cast<std::size_t>(category)]; }
    // These defaults with every field `later` sets taken from it.
    [[nodiscard]] QcHoldDefaults merged(const QcHoldDefaults &later) const;
    bool operator==(const QcHoldDefaults &) const = default;
};

namespace QcHoldNames {

// The JSON names of categories and grips ("tool", "partialBlock", "handle", ...).
QString category(QcHoldCategory category);
std::optional<QcHoldCategory> category(const QString &name);
QString grip(QcHoldGrip grip);
std::optional<QcHoldGrip> grip(const QString &name);

} // namespace QcHoldNames
