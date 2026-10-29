// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/parser/QcContentFields.h"

#include "common/content/parser/QcJsonFieldReader.h"
#include "common/content/registry/QcContentNames.h"

#include <QHash>
#include <QJsonArray>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

constexpr qsizetype maxCategoryLength = 64;

} // namespace

namespace QcContentFields {

bool isValidCategory(const QString &category)
{
    return category.size() <= maxCategoryLength && !category.contains(u'/') && QcContentNames::isValidPart(category);
}

QString invalidCategoryMessage()
{
    return u"'category' must be a name of lower-case letters, digits, '_', '-' and '.' (at most 64 characters)"_s;
}

QString localPart(const QString &qualifiedName)
{
    return qualifiedName.mid(qualifiedName.indexOf(u':') + 1);
}

QString translationKey(const QString &prefix, const QString &qualifiedName)
{
    return u"%1.%2.%3"_s.arg(prefix, qualifiedName.left(qualifiedName.indexOf(u':')), localPart(qualifiedName).replace(u'/', u'.'));
}

QString qualifyIngredient(const QString &text, const QString &packNamespace)
{
    if (text.startsWith(u'#')) {
        const QString tag = QcContentNames::qualify(text.mid(1), packNamespace);
        return tag.isEmpty() ? QString() : u'#' + tag;
    }
    return QcContentNames::qualify(text, packNamespace);
}

QStringList parseTags(const QJsonValue &value, const QString &packNamespace, QcJsonFieldReader &reader)
{
    QStringList tags;
    if (value.isUndefined()) {
        return tags;
    }

    const QJsonArray list = value.toArray();
    if (!value.isArray() || list.size() > 16) {
        reader.fail(u"'tags' must be an array of at most 16 names"_s);
        return tags;
    }

    for (const auto &entry : list) {
        const QString tag = entry.isString() ? QcContentNames::qualify(entry.toString(), packNamespace) : QString();
        if (tag.isEmpty()) {
            reader.fail(u"invalid tag"_s);
            return {};
        }
        tags.append(tag);
    }
    return tags;
}

QStringList parseNames(const QString &key, const QString &packNamespace, QcJsonFieldReader &reader)
{
    const QJsonValue value = reader.json.value(key);
    if (value.isUndefined()) {
        return {};
    }

    const QJsonArray list = value.toArray();
    if (!value.isArray() || list.size() > 64) {
        reader.fail(u"'%1' must be an array of at most 64 names"_s.arg(key));
        return {};
    }

    QStringList names;
    for (const auto &entry : list) {
        const QString name = entry.isString() ? QcContentNames::qualify(entry.toString(), packNamespace) : QString();
        if (name.isEmpty()) {
            reader.fail(u"invalid '%1' entry"_s.arg(key));
            return {};
        }
        names.append(name);
    }
    return names;
}

[[nodiscard]] std::optional<QString> stateValue(const QJsonValue &value)
{
    if (value.isString()) {
        return value.toString();
    }
    if (value.isBool()) {
        return value.toBool() ? u"true"_s : u"false"_s;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<QcVec3> vectorField(const QJsonValue &value, double limit)
{
    const QJsonArray array = value.toArray();
    if (!value.isArray() || array.size() != 3 || !array.at(0).isDouble() || !array.at(1).isDouble() || !array.at(2).isDouble()) {
        return std::nullopt;
    }

    const QcVec3 vector{array.at(0).toDouble(), array.at(1).toDouble(), array.at(2).toDouble()};
    if (std::abs(vector.x) > limit || std::abs(vector.y) > limit || std::abs(vector.z) > limit) {
        return std::nullopt;
    }
    return vector;
}

[[nodiscard]] std::optional<QcBoneRole> boneRoleFromName(const QString &name)
{
    static const QHash<QString, QcBoneRole> roles = {
        {u"none"_s, QcBoneRole::None},          {u"head"_s, QcBoneRole::Head},
        {u"body"_s, QcBoneRole::Body},          {u"right_arm"_s, QcBoneRole::RightArm},
        {u"left_arm"_s, QcBoneRole::LeftArm},   {u"right_leg"_s, QcBoneRole::RightLeg},
        {u"left_leg"_s, QcBoneRole::LeftLeg},
    };
    const auto it = roles.constFind(name);
    return it == roles.constEnd() ? std::nullopt : std::optional<QcBoneRole>(*it);
}

QcExperienceDrop parseExperience(const QString &key, QcJsonFieldReader &reader, QcExperienceDrop fallback)
{
    const QJsonValue value = reader.json.value(key);
    if (value.isUndefined()) {
        return fallback;
    }

    const auto valid = [](double points) { return std::isfinite(points) && points >= 0.0 && points <= maxExperience && std::floor(points) == points; };
    if (value.isDouble() && valid(value.toDouble())) {
        return {static_cast<int>(value.toDouble()), static_cast<int>(value.toDouble())};
    }

    const QJsonArray range = value.toArray();
    if (value.isArray() && range.size() == 2 && range[0].isDouble() && range[1].isDouble() && valid(range[0].toDouble())
        && valid(range[1].toDouble()) && range[0].toDouble() <= range[1].toDouble()) {
        return {static_cast<int>(range[0].toDouble()), static_cast<int>(range[1].toDouble())};
    }

    reader.fail(u"'%1' must be a whole number or [min, max] of whole numbers in 0..%2"_s.arg(key).arg(maxExperience));
    return fallback;
}

void checkFormatVersion(int supported, QcJsonFieldReader &reader)
{
    const QJsonValue value = reader.json.value(u"formatVersion"_s);
    if (value.isUndefined()) {
        return;
    }
    const double version = value.toDouble(-1.0);
    if (!value.isDouble() || version < 1.0 || version != std::floor(version)) {
        reader.fail(u"'formatVersion' must be a whole number from 1"_s);
    } else if (version > supported) {
        reader.fail(u"formatVersion %1 is newer than this game reads (up to %2); the file is left out"_s.arg(version).arg(supported));
    }
}

} // namespace QcContentFields
