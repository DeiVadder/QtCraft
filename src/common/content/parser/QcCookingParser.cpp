// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/parser/QcCookingParser.h"

#include "common/content/definitions/QcItemDefinition.h"
#include "common/content/definitions/QcSmeltingRecipe.h"
#include "common/content/definitions/QcSoundKeys.h"
#include "common/content/parser/QcContentFields.h"
#include "common/content/parser/QcJsonFieldReader.h"

#include <QJsonArray>

using namespace Qt::StringLiterals;

namespace {

constexpr double minMachineSpeed = 0.01;
constexpr double maxMachineSpeed = 100.0;
constexpr qsizetype maxKinds = 16;

// The recipe category of a machine or campfire (default: the base furnace's).
QString recipeCategory(QcJsonFieldReader &reader)
{
    const QString category = reader.string(u"category"_s, QString::fromLatin1(QcSmeltingRecipe::defaultCategory));
    if (reader.ok() && !QcContentFields::isValidCategory(category)) {
        reader.fail(QcContentFields::invalidCategoryMessage());
    }
    return category;
}

// The recipe kinds of "kinds" (QcSmeltingRecipe::kind); empty when the field is missing.
QStringList recipeKinds(const QJsonObject &json, QcJsonFieldReader &reader)
{
    const QJsonValue kinds = json.value(u"kinds"_s);
    if (kinds.isUndefined()) {
        return {};
    }
    if (!kinds.isArray() || kinds.toArray().isEmpty() || kinds.toArray().size() > maxKinds) {
        reader.fail(u"'kinds' must be an array of 1 to 16 recipe kinds"_s);
        return {};
    }

    QStringList list;
    const QJsonArray entries = kinds.toArray();
    for (const auto &entry : entries) {
        const QString kind = entry.toString();
        if (!entry.isString() || !QcContentFields::isValidCategory(kind)) {
            reader.fail(u"invalid 'kinds' entry"_s);
            return {};
        }
        list.append(kind);
    }
    return list;
}

} // namespace

namespace QcCookingParser {

QcMachineDefinition parseMachine(const QJsonObject &json, QcJsonFieldReader &reader, const QString &packNamespace)
{
    QcMachineDefinition machine;
    machine.category = recipeCategory(reader);
    machine.speed = reader.number(u"speed"_s, 1.0, minMachineSpeed, maxMachineSpeed);
    machine.sound = reader.name(u"sound"_s, packNamespace, QString::fromLatin1(QcSoundKeys::machineActive));
    machine.kinds = recipeKinds(json, reader);
    return machine;
}

QcCampfireDefinition parseCampfire(const QJsonObject &json, QcJsonFieldReader &reader, const QString &packNamespace)
{
    QcCampfireDefinition campfire;
    campfire.slotCount = reader.integer(u"slots"_s, 0, 0, QcCampfireDefinition::maxSlots);
    campfire.seconds = reader.number(u"seconds"_s, campfire.seconds, QcSmeltingRecipe::minSeconds, QcCampfireDefinition::maxSeconds);
    campfire.category = recipeCategory(reader);
    campfire.kinds = recipeKinds(json, reader);
    campfire.extinguishesInto = reader.name(u"extinguishesInto"_s, packNamespace);
    campfire.lightsInto = reader.name(u"lightsInto"_s, packNamespace);
    if (reader.ok() && !campfire.isCampfire()) {
        reader.fail(u"a campfire needs 'slots' or 'lightsInto'"_s);
    }
    return campfire;
}

QcBitesDefinition parseBites(const QcBlockDefinition &block, QcJsonFieldReader &reader)
{
    QcBitesDefinition bites;
    bites.food = reader.integer(u"food"_s, 2, 1, QcItemDefinition::maxFood);
    const QcBlockProperty *property = block.property(u"bites"_s);
    if (reader.ok() && (!property || property->values.size() < 2)) {
        reader.fail(u"a block with bites needs the state 'bites' with at least two values"_s);
    }
    return bites;
}

} // namespace QcCookingParser
