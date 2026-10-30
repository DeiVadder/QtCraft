// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/items/QcExperienceDrop.h"
#include "common/content/mobs/QcModelDefinition.h"
#include "common/core/QcVec3.h"

#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <optional>

struct QcJsonFieldReader;

/*! \brief Field conventions shared by the content-pack parsers. */
namespace QcContentFields {

/*! The longest burn time in seconds a block or item may give as fuel (\c {"fuelSeconds"}). */
inline constexpr double maxFuelSeconds = 100000.0;

/*! Returns whether \a category is a valid recipe category: at most 64 characters of a name part
    without '/' and without namespace, so that every pack can add recipes to "smelting". */
bool isValidCategory(const QString &category);
/*! Returns the warning text for a category that isValidCategory() refuses. */
QString invalidCategoryMessage();

/*! Returns \a qualifiedName without its namespace (the part after the first ':'; the whole text
    when there is none). */
QString localPart(const QString &qualifiedName);
/*! Returns "<prefix>.<namespace>.<path with '/' as '.'>" for \a prefix and \a qualifiedName, the
    default translation key of a content name. */
QString translationKey(const QString &prefix, const QString &qualifiedName);
/*! Returns the recipe ingredient \a text qualified with \a packNamespace: an item name or, with a
    leading '#', an item tag; empty when the name is invalid. */
QString qualifyIngredient(const QString &text, const QString &packNamespace);
/*! Returns the tags in \a value (an array of at most 16 names, qualified with \a packNamespace;
    missing: empty). Anything else fails \a reader and gives an empty list. */
QStringList parseTags(const QJsonValue &value, const QString &packNamespace, QcJsonFieldReader &reader);
/*! Returns the array of content names at \a key of the object of \a reader, qualified with
    \a packNamespace (at most 64; missing: empty). Anything else fails \a reader and gives an
    empty list. */
QStringList parseNames(const QString &key, const QString &packNamespace, QcJsonFieldReader &reader);
/*! Returns the state value in \a value: strings as they are, booleans as "true"/"false"; none for
    any other JSON type. */
[[nodiscard]] std::optional<QString> stateValue(const QJsonValue &value);
/*! Returns the three numbers in \a value, each within \a limit metres of 0 (model coordinates);
    none for anything else. */
[[nodiscard]] std::optional<QcVec3> vectorField(const QJsonValue &value, double limit = 8.0);
/*! Returns the bone role named \a name ("none", "head", "body", "right_arm", "left_arm",
    "right_leg", "left_leg" for skeleton models); none for an unknown name. */
[[nodiscard]] std::optional<QcBoneRole> boneRoleFromName(const QString &name);

/*! The most experience points a content field may give. */
inline constexpr int maxExperience = 100000;
/*! Returns the experience points at \a key of the object of \a reader (decision E-398): a whole
    number, or [min, max] (min not above max) for an even choice between them, each in
    0..maxExperience; missing: \a fallback. Anything else fails \a reader and gives \a fallback. */
QcExperienceDrop parseExperience(const QString &key, QcJsonFieldReader &reader, QcExperienceDrop fallback = {});

/*! The format of the village files (styles, worldgen/villages.json, villager settings,
    professions and trades, decision E-421): their "formatVersion", 1 when missing. */
inline constexpr int villageFormatVersion = 1;
/*! Fails \a reader when its "formatVersion" is no whole number from 1 or newer than
    \a supported, so a file written for a later game is left out with a clear warning rather than
    read wrongly. A missing "formatVersion" passes. */
void checkFormatVersion(int supported, QcJsonFieldReader &reader);

} // namespace QcContentFields
