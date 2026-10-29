// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/definitions/QcExperienceDrop.h"
#include "common/content/definitions/QcModelDefinition.h"
#include "common/core/QcVec3.h"

#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <optional>

struct QcJsonFieldReader;

// Field conventions shared by the content-pack parsers.
namespace QcContentFields {

inline constexpr double maxFuelSeconds = 100000.0;

// Categories are plain names without namespace, so that every pack can add recipes to "smelting".
bool isValidCategory(const QString &category);
QString invalidCategoryMessage();

QString localPart(const QString &qualifiedName);
// "<prefix>.<namespace>.<path with '/' as '.'>", the default translation key of a content name.
QString translationKey(const QString &prefix, const QString &qualifiedName);
// Recipe ingredients are item names or, with a leading '#', item tags.
QString qualifyIngredient(const QString &text, const QString &packNamespace);
QStringList parseTags(const QJsonValue &value, const QString &packNamespace, QcJsonFieldReader &reader);
// The array of content names at `key` (at most 64; missing: empty).
QStringList parseNames(const QString &key, const QString &packNamespace, QcJsonFieldReader &reader);
// A state value from JSON: strings as they are, booleans as "true"/"false".
[[nodiscard]] std::optional<QString> stateValue(const QJsonValue &value);
// Three numbers, each within `limit` metres of 0 (model coordinates).
[[nodiscard]] std::optional<QcVec3> vectorField(const QJsonValue &value, double limit = 8.0);
// Bone roles of skeleton models: "none", "head", "body", "right_arm", "left_arm", "right_leg", "left_leg".
[[nodiscard]] std::optional<QcBoneRole> boneRoleFromName(const QString &name);

// Experience points at `key` (decision E-398): a number, or [min, max] for an even choice between
// them, each in 0..maxExperience; missing: `fallback`.
inline constexpr int maxExperience = 100000;
QcExperienceDrop parseExperience(const QString &key, QcJsonFieldReader &reader, QcExperienceDrop fallback = {});

// The format of the village files (styles, worldgen/villages.json, villager settings, professions
// and trades, decision E-421): their "formatVersion", 1 when missing.
inline constexpr int villageFormatVersion = 1;
// Fails the reader when "formatVersion" is no whole number from 1 or newer than `supported`, so a
// file written for a later game is left out with a clear warning rather than read wrongly.
void checkFormatVersion(int supported, QcJsonFieldReader &reader);

} // namespace QcContentFields
