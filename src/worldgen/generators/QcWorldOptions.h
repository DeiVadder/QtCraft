// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

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

/*! \brief The values of the world generation options of one world (decision E-917): world.json
    keeps them as {"<option>": number or true/false} in "generator"/"worldOptions", written in full
    when the world is created, so that later changes of the content's defaults do not change it.

    A world without them (made before the options, or of a type without options) generates with
    the content's values alone. The menu (QcWorldOptionsController), qtcraft-headless
    (--world-options) and a later map preview read the options through here; the generator and
    the server get a copy of the content with the values set (apply). */
namespace QcWorldOptions {

/*! At most this many values per world. */
inline constexpr qsizetype maxValues = 256;

/*! \brief A usable option of the content: its definition, its target and its default. */
struct Option
{
    /*! The option's definition in the content. */
    const QcWorldOptionDefinition *definition = nullptr;
    /*! The generator value the option sets. */
    const QcWorldOptionTargets::Target *target = nullptr;
    /*! The definition's default, or else the value the target reads from the content. */
    double defaultValue = 0.0;

    /*! Returns whether the option is on or off rather than a number. */
    [[nodiscard]] bool isSwitch() const { return target->kind == QcWorldOptionTargets::Kind::Switch; }
    /*! Returns \a value as world.json writes it: true/false for a switch, else the number. */
    [[nodiscard]] QJsonValue json(double value) const;
    /*! Returns the value of \a json for this option; empty when it is of the wrong type or
        outside the option's range (\a strict) or the target's. */
    [[nodiscard]] std::optional<double> read(const QJsonValue &json, bool strict) const;
    /*! Returns \a value on the option's grid (min + n · step, written with the step's decimals,
        so that 0.6 is the same double as the 0.6 of a JSON file), and exactly the default when
        it lies within rounding of it; a switch's value as it is. A slider's 0.6000000000000001
        thus becomes 0.6. A value outside the range stays outside (read() refuses it). */
    [[nodiscard]] double snapped(double value) const;
};

/*! \brief The result of apply: the content to generate and serve the world with, and what was
    ignored. */
struct Applied
{
    /*! The content to generate and serve the world with. */
    std::shared_ptr<const QcContentRegistry> content;
    /*! What was ignored: unknown options and invalid values. */
    QStringList warnings;
};

/*! Returns whether worlds of the generator type \a type take options (terrain worlds). */
[[nodiscard]] bool takesOptions(const QString &type);
/*! Returns the options of \a content whose target, subject, group, range and default fit, in
    content order. */
[[nodiscard]] QList<Option> optionsOf(const QcContentRegistry &content);
/*! Returns one warning per option or preset value of \a content that does not fit (checked with
    the mods, QcModLoader). */
[[nodiscard]] QStringList check(const QcContentRegistry &content);
/*! Returns every usable option of \a content at its default. */
[[nodiscard]] QJsonObject defaults(const QcContentRegistry &content);
/*! Returns the values a new world of \a type stores: \a given over the defaults of \a content;
    an unknown option or a value of the wrong type or outside its range is refused. Worlds of
    other types store none. */
[[nodiscard]] QcResult<QJsonObject> valuesToStore(const QString &type, const QJsonObject &given, const QcContentRegistry &content);
/*! Returns the values of the preset \a name (qualified, or of the base game) over the defaults,
    both of \a content; values that do not fit are left out. */
[[nodiscard]] QcResult<QJsonObject> presetValues(const QString &name, const QcContentRegistry &content);
/*! Returns the name of the preset of \a content whose values are exactly \a values; empty when
    there is none. */
[[nodiscard]] QString presetOf(const QJsonObject &values, const QcContentRegistry &content);
/*! Returns the values \a text names, options of \a content as written on a command line:
    "preset=more_villages,village_spacing=256,caves=off" (names without namespace are the base
    game's; switches on/off or true/false). Only the values named, a preset's included;
    valuesToStore fills in the rest. Presets apply first, in the order given, then the single
    values, so "village_chance=0.5,preset=more_villages" keeps 0.5. */
[[nodiscard]] QcResult<QJsonObject> parseText(const QString &text, const QcContentRegistry &content);
/*! Returns a copy of \a content with \a values, the values of a world of \a type, set; the same
    content when there are none or the type takes no options. */
[[nodiscard]] Applied apply(std::shared_ptr<const QcContentRegistry> content, const QString &type, const QJsonObject &values);

} // namespace QcWorldOptions
