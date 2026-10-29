// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/registry/QcAdvancementParser.h"

#include "common/content/registry/QcContentFields.h"
#include "common/content/registry/QcContentNames.h"
#include "common/content/registry/QcJsonFieldReader.h"
#include "common/core/QcWorldConstants.h"

#include <QJsonArray>

#include <array>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

struct TriggerEntry
{
    QLatin1StringView name;
    QcAdvancementTrigger trigger = QcAdvancementTrigger::Impossible;
    // The condition fields the trigger understands.
    std::initializer_list<QLatin1StringView> conditions;
};

const std::array<TriggerEntry, 17> &triggerTable()
{
    using T = QcAdvancementTrigger;
    using L = QLatin1StringView;
    static const std::array<TriggerEntry, 17> table{{
        {L("impossible"), T::Impossible, {}},
        {L("item_obtained"), T::ItemObtained, {L("item"), L("count")}},
        {L("block_placed"), T::BlockPlaced, {L("block")}},
        {L("block_broken"), T::BlockBroken, {L("block")}},
        {L("item_crafted"), T::ItemCrafted, {L("item")}},
        {L("item_smelted"), T::ItemSmelted, {L("item")}},
        {L("entity_killed"), T::EntityKilled, {L("entity")}},
        {L("entity_bred"), T::EntityBred, {L("entity")}},
        {L("entity_tamed"), T::EntityTamed, {L("entity")}},
        {L("location"), T::Location, {L("biome"), L("structure"), L("minY"), L("maxY")}},
        {L("item_used_on_block"), T::ItemUsedOnBlock, {L("item"), L("block")}},
        {L("item_used_on_entity"), T::ItemUsedOnEntity, {L("item"), L("entity")}},
        {L("ride_vehicle"), T::RideVehicle, {L("vehicle")}},
        {L("villager_trade"), T::VillagerTrade, {L("item")}},
        {L("item_enchanted"), T::ItemEnchanted, {L("item")}},
        {L("effect_gained"), T::EffectGained, {L("effect")}},
        {L("level_reached"), T::LevelReached, {L("level")}},
    }};
    return table;
}

const TriggerEntry &entryOf(QcAdvancementTrigger trigger)
{
    return triggerTable()[static_cast<std::size_t>(trigger)];
}

// The conditions of a criterion; fields the trigger does not know fail the reader.
QcAdvancementConditions parseConditions(const QJsonObject &json, const TriggerEntry &trigger, const QString &pack, QcJsonFieldReader &reader)
{
    for (auto it = json.constBegin(); it != json.constEnd(); ++it) {
        if (std::ranges::find(trigger.conditions, it.key()) == trigger.conditions.end()) {
            reader.fail(u"trigger '%1' has no condition '%2'"_s.arg(trigger.name, it.key()));
            return {};
        }
    }

    QcAdvancementConditions conditions;
    conditions.item = QcContentFields::qualifyIngredient(reader.string(u"item"_s), pack);
    conditions.block = QcContentFields::qualifyIngredient(reader.string(u"block"_s), pack);
    conditions.entity = reader.name(u"entity"_s, pack);
    conditions.biome = reader.name(u"biome"_s, pack);
    conditions.structure = reader.string(u"structure"_s);
    if (!conditions.structure.isEmpty() && conditions.structure != u"village"_s) {
        reader.fail(u"'structure' must be \"village\""_s);
    }
    conditions.effect = reader.name(u"effect"_s, pack);
    conditions.vehicle = reader.string(u"vehicle"_s);
    if (!conditions.vehicle.isEmpty() && conditions.vehicle != u"minecart"_s && conditions.vehicle != u"boat"_s) {
        reader.fail(u"'vehicle' must be \"minecart\" or \"boat\""_s);
    }
    if (json.contains(u"minY"_s)) {
        conditions.minY = reader.integer(u"minY"_s, 0, 0, QcWorld::worldHeight);
    }
    if (json.contains(u"maxY"_s)) {
        conditions.maxY = reader.integer(u"maxY"_s, 0, 0, QcWorld::worldHeight);
    }
    if (conditions.minY && conditions.maxY && *conditions.minY > *conditions.maxY) {
        reader.fail(u"'minY' is above 'maxY'"_s);
    }
    conditions.count = reader.integer(u"count"_s, 1, 1, 64 * 36);
    conditions.level = reader.integer(u"level"_s, 0, 0, 10000);
    return conditions;
}

std::optional<QcAdvancementCriterion> parseCriterion(const QString &name, const QJsonValue &value, const QString &pack, QcJsonFieldReader &reader)
{
    if (!value.isObject()) {
        reader.fail(u"criterion '%1' must be an object"_s.arg(name));
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    QcJsonFieldReader nested{object, u"%1 criterion '%2'"_s.arg(reader.context, name), {}};
    const QString triggerText = nested.string(u"trigger"_s);
    const std::optional<QcAdvancementTrigger> trigger = QcAdvancementParser::triggerOf(triggerText);
    if (!trigger) {
        reader.fail(u"criterion '%1': unknown trigger '%2'"_s.arg(name, triggerText));
        return std::nullopt;
    }

    QcAdvancementCriterion criterion{name, *trigger, {}};
    nested.nestedObject(u"conditions"_s, [&](const QJsonObject &conditions, QcJsonFieldReader &inner) {
        criterion.conditions = parseConditions(conditions, entryOf(*trigger), pack, inner);
    });
    reader.adopt(nested);
    return criterion;
}

QList<QcAdvancementCriterion> parseCriteria(const QString &pack, QcJsonFieldReader &reader)
{
    const QJsonValue value = reader.json.value(u"criteria"_s);
    const QJsonObject object = value.toObject();
    if (!value.isObject() || object.isEmpty() || object.size() > QcAdvancementDefinition::maxCriteria) {
        reader.fail(u"'criteria' must be an object of 1 to %1 criteria"_s.arg(QcAdvancementDefinition::maxCriteria));
        return {};
    }

    QList<QcAdvancementCriterion> criteria;
    for (auto it = object.constBegin(); it != object.constEnd() && reader.ok(); ++it) {
        if (it.key().isEmpty() || it.key().size() > 64) {
            reader.fail(u"criterion names must have 1 to 64 characters"_s);
            break;
        }
        if (std::optional<QcAdvancementCriterion> criterion = parseCriterion(it.key(), it.value(), pack, reader)) {
            criteria.append(std::move(*criterion));
        }
    }
    return criteria;
}

// "all" (default; every criterion is needed), "any" (one is enough) or a list of groups of criterion
// names, each group met by one of its criteria.
QList<QStringList> parseRequirements(const QList<QcAdvancementCriterion> &criteria, QcJsonFieldReader &reader)
{
    const QJsonValue value = reader.json.value(u"requirements"_s);
    QStringList names;
    for (const QcAdvancementCriterion &criterion : criteria) {
        names.append(criterion.name);
    }
    if (value.isUndefined() || (value.isString() && value.toString() == u"all"_s)) {
        QList<QStringList> groups;
        for (const QString &name : std::as_const(names)) {
            groups.append(QStringList{name});
        }
        return groups;
    }
    if (value.isString() && value.toString() == u"any"_s) {
        return {names};
    }

    const QJsonArray list = value.toArray();
    if (!value.isArray() || list.isEmpty() || list.size() > QcAdvancementDefinition::maxCriteria) {
        reader.fail(u"'requirements' must be \"all\", \"any\" or a list of 1 to %1 groups"_s.arg(QcAdvancementDefinition::maxCriteria));
        return {};
    }
    QList<QStringList> groups;
    for (const auto &groupValue : list) {
        const QStringList group = groupValue.toVariant().toStringList();
        const bool known = std::ranges::all_of(group, [&names](const QString &name) { return names.contains(name); });
        if (!groupValue.isArray() || group.isEmpty() || group.size() > QcAdvancementDefinition::maxCriteria || !known) {
            reader.fail(u"every group of 'requirements' must list 1 to %1 criteria of the advancement"_s.arg(QcAdvancementDefinition::maxCriteria));
            return {};
        }
        groups.append(group);
    }
    return groups;
}

std::optional<QcAdvancementFrame> frameOf(const QString &text)
{
    if (text == u"task"_s) {
        return QcAdvancementFrame::Task;
    }
    if (text == u"goal"_s) {
        return QcAdvancementFrame::Goal;
    }
    if (text == u"challenge"_s) {
        return QcAdvancementFrame::Challenge;
    }
    return std::nullopt;
}

// "<kind>:<name>" with the pack's namespace added to a name without one; empty when unset.
QString parseHandbookLink(const QString &pack, QcJsonFieldReader &reader)
{
    const QString text = reader.string(u"handbook"_s);
    if (text.isEmpty()) {
        return {};
    }
    static constexpr std::array<QLatin1StringView, 5> kinds{QLatin1StringView("chapter"), QLatin1StringView("item"), QLatin1StringView("block"),
                                                           QLatin1StringView("mob"), QLatin1StringView("biome")};
    const qsizetype colon = text.indexOf(u':');
    const QString kind = text.left(colon);
    const QString name = colon < 0 ? QString() : QcContentNames::qualify(text.mid(colon + 1), pack);
    if (std::ranges::find(kinds, kind) == kinds.end() || name.isEmpty()) {
        reader.fail(u"'handbook' must be \"<chapter|item|block|mob|biome>:<name>\""_s);
        return {};
    }
    return kind + u':' + name;
}

} // namespace

namespace QcAdvancementParser {

std::optional<QcAdvancementTrigger> triggerOf(const QString &text)
{
    for (const TriggerEntry &entry : triggerTable()) {
        if (text == entry.name) {
            return entry.trigger;
        }
    }
    return std::nullopt;
}

QString triggerName(QcAdvancementTrigger trigger)
{
    return entryOf(trigger).name.toString();
}

QString frameName(QcAdvancementFrame frame)
{
    switch (frame) {
    case QcAdvancementFrame::Goal:
        return u"goal"_s;
    case QcAdvancementFrame::Challenge:
        return u"challenge"_s;
    case QcAdvancementFrame::Task:
        break;
    }
    return u"task"_s;
}

QcResult<QcAdvancementDefinition> parseAdvancement(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"advancement"_s, {}};
    QcAdvancementDefinition advancement;
    advancement.name = reader.name(u"name"_s, packNamespace);
    if (advancement.name.isEmpty()) {
        return qcError(reader.ok() ? u"advancement: 'name' is required"_s : reader.error);
    }
    if (advancement.name.size() > QcAdvancementDefinition::maxNameLength) {
        return qcError(u"advancement '%1': the name is longer than %2 characters"_s.arg(advancement.name).arg(QcAdvancementDefinition::maxNameLength));
    }
    reader.context = u"advancement '%1'"_s.arg(advancement.name);
    advancement.disabled = reader.boolean(u"disabled"_s, false);
    if (advancement.disabled) {
        return reader.ok() ? QcResult<QcAdvancementDefinition>(std::move(advancement)) : qcError(reader.error);
    }

    advancement.parent = reader.name(u"parent"_s, packNamespace);
    advancement.icon = reader.name(u"icon"_s, packNamespace);
    advancement.titleKey = reader.string(u"title"_s, QcContentFields::translationKey(u"advancement"_s, advancement.name));
    advancement.descriptionKey = reader.string(u"description"_s, advancement.titleKey + u".description"_s);
    advancement.handbook = parseHandbookLink(packNamespace, reader);
    const QString frame = reader.string(u"frame"_s, u"task"_s);
    if (const std::optional<QcAdvancementFrame> known = frameOf(frame)) {
        advancement.frame = *known;
    } else {
        reader.fail(u"unknown 'frame' '%1'"_s.arg(frame));
    }
    advancement.hidden = reader.boolean(u"hidden"_s, false);
    advancement.announce = reader.boolean(u"announce"_s, true);
    advancement.criteria = parseCriteria(packNamespace, reader);
    advancement.requirements = parseRequirements(advancement.criteria, reader);
    if (advancement.icon.isEmpty()) {
        reader.fail(u"'icon' is required"_s);
    }
    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return advancement;
}

} // namespace QcAdvancementParser
