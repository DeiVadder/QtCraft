#include "QcVillagerParser.h"

#include "QcContentFields.h"
#include "QcContentNames.h"
#include "QcMobParser.h"

#include <QJsonArray>

#include <algorithm>
#include <optional>

using namespace Qt::StringLiterals;

namespace {

// "#rrggbb" as 0xRRGGBB.
[[nodiscard]] std::optional<quint32> colourOf(const QJsonValue &value)
{
    const QString text = value.toString();
    if (text.size() != 7 || !text.startsWith(u'#')) {
        return std::nullopt;
    }
    bool ok = false;
    const quint32 rgb = QStringView(text).mid(1).toUInt(&ok, 16);
    return ok ? std::optional<quint32>(rgb) : std::nullopt;
}

QList<quint32> readColours(const QJsonObject &json, const QString &key, QcJsonFieldReader &reader)
{
    QList<quint32> colours;
    const QJsonValue value = json.value(key);
    if (value.isUndefined()) {
        return colours;
    }
    const QJsonArray array = value.toArray();
    if (!value.isArray() || array.size() > QcVillagerLook::maxChoices) {
        reader.fail(u"'%1' must be a list of at most %2 colours"_s.arg(key).arg(QcVillagerLook::maxChoices));
        return colours;
    }
    for (const auto &entry : array) {
        const std::optional<quint32> colour = colourOf(entry);
        if (!colour) {
            reader.fail(u"'%1' holds colours as \"#rrggbb\""_s.arg(key));
            return {};
        }
        colours.append(*colour);
    }
    return colours;
}

// Hair styles by name; an empty string stands for no hair.
QStringList readHair(const QJsonObject &json, const QString &packNamespace, QcJsonFieldReader &reader)
{
    QStringList hair;
    const QJsonArray array = json.value(u"hair"_s).toArray();
    if (array.size() > QcVillagerLook::maxChoices) {
        reader.fail(u"'hair' lists at most %1 styles"_s.arg(QcVillagerLook::maxChoices));
        return hair;
    }
    for (const auto &entry : array) {
        const QString name = entry.toString();
        hair.append(name.isEmpty() ? QString() : QcContentNames::qualify(name, packNamespace));
    }
    return hair;
}

[[nodiscard]] std::optional<QcVillagerActivity> activityFromName(const QString &name)
{
    static const QHash<QString, QcVillagerActivity> activities = {
        {u"wander"_s, QcVillagerActivity::Wander}, {u"work"_s, QcVillagerActivity::Work}, {u"meet"_s, QcVillagerActivity::Meet},
        {u"home"_s, QcVillagerActivity::Home},     {u"sleep"_s, QcVillagerActivity::Sleep},
    };
    const auto it = activities.constFind(name);
    return it == activities.constEnd() ? std::nullopt : std::optional(*it);
}

QList<QcVillagerRoutineStep> readRoutine(const QJsonObject &json, QcJsonFieldReader &reader)
{
    QList<QcVillagerRoutineStep> routine;
    const QJsonArray array = json.value(u"routine"_s).toArray();
    if (array.isEmpty() || array.size() > QcVillagerSettings::maxRoutineSteps) {
        reader.fail(u"'routine' needs 1 to %1 steps"_s.arg(QcVillagerSettings::maxRoutineSteps));
        return routine;
    }
    for (const auto &entry : array) {
        QcJsonFieldReader step{entry.toObject(), reader.context + u" routine"_s, {}};
        const double from = step.number(u"from"_s, -1.0, 0.0, 1.0);
        const std::optional<QcVillagerActivity> activity = activityFromName(step.string(u"do"_s));
        if (!entry.isObject() || from < 0.0 || from >= 1.0 || !activity) {
            step.fail(u"every step needs 'from' in [0, 1) and 'do' (wander, work, meet, home or sleep)"_s);
        }
        reader.adopt(step);
        routine.append({from, activity.value_or(QcVillagerActivity::Wander)});
    }
    std::ranges::stable_sort(routine, {}, &QcVillagerRoutineStep::from);
    return routine;
}

// The layers a profession lays over the style's outfit, read as the layers of an outfit.
QList<QcAppearanceLayer> readLayers(const QJsonObject &json, const QString &name, const QString &packNamespace, QcJsonFieldReader &reader)
{
    if (!json.contains(u"layers"_s)) {
        return {};
    }
    const QJsonObject outfit{{u"name"_s, name}, {u"kind"_s, u"outfit"_s}, {u"layers"_s, json.value(u"layers"_s)}};
    QcResult<QcAppearanceDefinition> parsed = QcMobParser::parseAppearance(outfit, packNamespace);
    if (!parsed) {
        reader.fail(parsed.error());
        return {};
    }
    return parsed->layers;
}

QcVillagerProfession readProfession(const QJsonValue &value, const QString &packNamespace, QcJsonFieldReader &reader)
{
    const QJsonObject json = value.toObject();
    QcJsonFieldReader nested{json, reader.context + u" profession"_s, {}};
    QcVillagerProfession profession;
    profession.name = nested.name(u"name"_s, packNamespace);
    if (!value.isObject() || profession.name.isEmpty()) {
        nested.fail(u"every profession needs a 'name'"_s);
    }
    nested.context = u"%1 profession '%2'"_s.arg(reader.context, profession.name);
    profession.translationKey = nested.string(u"translation"_s, QcContentFields::translationKey(u"profession"_s, profession.name));
    profession.workstation = nested.name(u"workstation"_s, packNamespace);
    profession.hat = nested.name(u"hat"_s, packNamespace);
    profession.tendsCrops = nested.boolean(u"tendsCrops"_s, false);
    profession.layers = readLayers(json, profession.name, packNamespace, nested);
    reader.adopt(nested);
    return profession;
}

QList<QcVillagerProfession> readProfessions(const QJsonObject &json, const QString &packNamespace, QcJsonFieldReader &reader)
{
    QList<QcVillagerProfession> professions;
    const QJsonArray array = json.value(u"professions"_s).toArray();
    if (array.isEmpty() || array.size() > QcVillagerProfession::maxProfessions) {
        reader.fail(u"'professions' needs 1 to %1 entries"_s.arg(QcVillagerProfession::maxProfessions));
        return professions;
    }
    for (const auto &entry : array) {
        QcVillagerProfession profession = readProfession(entry, packNamespace, reader);
        const bool twice = std::ranges::any_of(professions, [&profession](const QcVillagerProfession &other) {
            return other.name == profession.name || (!profession.workstation.isEmpty() && other.workstation == profession.workstation);
        });
        if (twice) {
            reader.fail(u"profession '%1' repeats a name or workstation"_s.arg(profession.name));
        }
        professions.append(std::move(profession));
    }
    return professions;
}

} // namespace

namespace QcVillagerParser {

QcVillagerLook readLook(const QString &key, const QString &packNamespace, QcJsonFieldReader &reader)
{
    QcVillagerLook look;
    reader.nestedObject(key, [&look, &packNamespace](const QJsonObject &object, QcJsonFieldReader &nested) {
        look.outfit = nested.name(u"outfit"_s, packNamespace);
        look.hair = readHair(object, packNamespace, nested);
        look.skin = readColours(object, u"skin"_s, nested);
        look.hairColor = readColours(object, u"hairColor"_s, nested);
        look.shirt = readColours(object, u"shirt"_s, nested);
        look.pants = readColours(object, u"pants"_s, nested);
        if (nested.ok() && look.outfit.isEmpty()) {
            nested.fail(u"needs an 'outfit'"_s);
        }
    });
    return look;
}

QcResult<QcVillagerSettings> parseVillagerSettings(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"villager settings"_s, {}};
    QcVillagerSettings settings;
    settings.mob = reader.name(u"mob"_s, packNamespace);
    settings.professions = readProfessions(json, packNamespace, reader);
    settings.unemployed = reader.name(u"unemployed"_s, packNamespace);
    settings.routine = readRoutine(json, reader);
    settings.look = readLook(u"look"_s, packNamespace, reader);
    settings.fleeDistance = reader.number(u"fleeDistance"_s, settings.fleeDistance, 0.0, 32.0);
    settings.shelterFromRain = reader.boolean(u"shelterFromRain"_s, settings.shelterFromRain);
    if (reader.ok() && (settings.mob.isEmpty() || !settings.look.isSet())) {
        reader.fail(u"needs a 'mob' and a 'look'"_s);
    }
    const QcVillagerProfession *unemployed = settings.profession(settings.unemployed);
    if (reader.ok() && (!unemployed || !unemployed->workstation.isEmpty())) {
        reader.fail(u"'unemployed' must name a profession without a workstation"_s);
    }
    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return settings;
}

} // namespace QcVillagerParser
