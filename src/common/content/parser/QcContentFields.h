#pragma once

#include "QcModelDefinition.h"
#include "QcVec3.h"

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
std::optional<QString> stateValue(const QJsonValue &value);
// Three numbers, each within `limit` metres of 0 (model coordinates).
std::optional<QcVec3> vectorField(const QJsonValue &value, double limit = 8.0);
// Bone roles of skeleton models: "none", "head", "body", "right_arm", "left_arm", "right_leg", "left_leg".
std::optional<QcBoneRole> boneRoleFromName(const QString &name);

} // namespace QcContentFields
