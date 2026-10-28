#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcResult.h"
#include "worldgen/generators/QcWorldOptionTargets.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

#include <memory>

// The values of the world generation options of one world (decision E-917): world.json keeps them
// as {"<option>": number or true/false} in "generator"/"worldOptions", written in full when the
// world is created, so that later changes of the content's defaults do not change it. A world
// without them (made before the options, or of a type without options) generates with the content's
// values alone. The menu (QcWorldOptionsController), qtcraft-headless (--world-options) and a later
// map preview read the options through here; the generator and the server get a copy of the content
// with the values set (apply).
namespace QcWorldOptions {

// At most this many values per world.
inline constexpr qsizetype maxValues = 256;

// A usable option of the content: its definition, its target and its default.
struct Option
{
    const QcWorldOptionDefinition *definition = nullptr;
    const QcWorldOptionTargets::Target *target = nullptr;
    double defaultValue = 0.0;

    [[nodiscard]] bool isSwitch() const { return target->kind == QcWorldOptionTargets::Kind::Switch; }
    // `value` as world.json writes it: true/false for a switch, else the number.
    [[nodiscard]] QJsonValue json(double value) const;
    // The value of `json` for this option; empty when it is of the wrong type or outside the
    // option's range (`strict`) or the target's.
    [[nodiscard]] std::optional<double> read(const QJsonValue &json, bool strict) const;
    // `value` on the option's grid (min + n · step, written with the step's decimals, so that 0.6 is
    // the same double as the 0.6 of a JSON file), and exactly the default when it lies within
    // rounding of it; a switch's value as it is. A slider's 0.6000000000000001 thus becomes 0.6. A
    // value outside the range stays outside (read() refuses it).
    [[nodiscard]] double snapped(double value) const;
};

// The result of apply: the content to generate and serve the world with, and what was ignored.
struct Applied
{
    std::shared_ptr<const QcContentRegistry> content;
    QStringList warnings;
};

// Whether worlds of the generator type `type` take options (terrain worlds).
[[nodiscard]] bool takesOptions(const QString &type);
// The options of `content` whose target, subject, group, range and default fit, in content order.
[[nodiscard]] QList<Option> optionsOf(const QcContentRegistry &content);
// One warning per option or preset value that does not fit (checked with the mods, QcModLoader).
[[nodiscard]] QStringList check(const QcContentRegistry &content);
// Every option at its default.
[[nodiscard]] QJsonObject defaults(const QcContentRegistry &content);
// The values a new world of `type` stores: `given` over the defaults; an unknown option or a value of
// the wrong type or outside its range is refused. Worlds of other types store none.
[[nodiscard]] QcResult<QJsonObject> valuesToStore(const QString &type, const QJsonObject &given, const QcContentRegistry &content);
// The values of the preset `name` (qualified, or of the base game) over the defaults.
[[nodiscard]] QcResult<QJsonObject> presetValues(const QString &name, const QcContentRegistry &content);
// The preset whose values are exactly `values`; empty when there is none.
[[nodiscard]] QString presetOf(const QJsonObject &values, const QcContentRegistry &content);
// Options as written on a command line: "preset=more_villages,village_spacing=256,caves=off"
// (names without namespace are the base game's; switches on/off or true/false). Only the values
// named, a preset's included; valuesToStore fills in the rest. Presets apply first, in the order
// given, then the single values, so "village_chance=0.5,preset=more_villages" keeps 0.5.
[[nodiscard]] QcResult<QJsonObject> parseText(const QString &text, const QcContentRegistry &content);
// `content` with the values of a world of `type` set; the same content when there are none.
[[nodiscard]] Applied apply(std::shared_ptr<const QcContentRegistry> content, const QString &type, const QJsonObject &values);

} // namespace QcWorldOptions
