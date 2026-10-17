#include "common/content/parser/QcVillagerParser.h"

#include "common/content/parser/QcContentFields.h"
#include "common/content/parser/QcMobParser.h"
#include "common/content/parser/QcTradeParser.h"
#include "common/content/registry/QcContentNames.h"

#include <QJsonArray>

#include <algorithm>
#include <optional>
#include <utility>

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

// Fails the reader for keys of an earlier format with where their content went (decision E-421).
void refuseMoved(const QList<std::pair<QString, QString>> &moved, QcJsonFieldReader &reader)
{
    for (const auto &[key, where] : moved) {
        if (reader.json.contains(key)) {
            reader.fail(u"'%1' moved to %2"_s.arg(key, where));
        }
    }
}

QStringList readWorkstationBlocks(const QString &packNamespace, QcJsonFieldReader &reader)
{
    const QStringList blocks = QcContentFields::parseNames(u"workstations"_s, packNamespace, reader);
    if (blocks.size() > QcVillagerProfession::maxWorkstationBlocks) {
        reader.fail(u"'workstations' lists at most %1 blocks"_s.arg(QcVillagerProfession::maxWorkstationBlocks));
    }
    return blocks;
}

QcVillagerBreedingSettings readBreeding(QcJsonFieldReader &reader)
{
    QcVillagerBreedingSettings breeding;
    reader.nestedObject(u"breeding"_s, [&breeding](const QJsonObject &, QcJsonFieldReader &nested) {
        breeding.food = nested.integer(u"food"_s, breeding.food, 0, 64);
        breeding.growUpSeconds = nested.number(u"growUpSeconds"_s, breeding.growUpSeconds, 1.0, 86400.0);
        breeding.cooldownSeconds = nested.number(u"cooldownSeconds"_s, breeding.cooldownSeconds, 0.0, 86400.0);
        breeding.partnerDistance = nested.number(u"partnerDistance"_s, breeding.partnerDistance, 1.0, 16.0);
        breeding.shareAbove = nested.integer(u"shareAbove"_s, breeding.shareAbove, 0, 1024);
        breeding.shareDistance = nested.number(u"shareDistance"_s, breeding.shareDistance, 0.0, 16.0);
        breeding.youngSpeed = nested.number(u"youngSpeed"_s, breeding.youngSpeed, 0.5, 3.0);
    });
    return breeding;
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
    settings.name = reader.name(u"name"_s, packNamespace);
    if (settings.name.isEmpty()) {
        return qcError(reader.ok() ? u"villager settings: 'name' is required"_s : reader.error);
    }

    reader.context = u"villager settings '%1'"_s.arg(settings.name);
    QcContentFields::checkFormatVersion(QcContentFields::villageFormatVersion, reader);
    refuseMoved({{u"mob"_s, u"the creature kind (\"villager\": \"<settings>\")"_s},
                 {u"professions"_s, u"professions/<name>.json"_s},
                 {u"golem"_s, u"worldgen/villages.json or the village style"_s}},
                reader);
    settings.unemployed = reader.name(u"unemployed"_s, packNamespace);
    settings.routine = readRoutine(json, reader);
    settings.look = readLook(u"look"_s, packNamespace, reader);
    settings.fleeDistance = reader.number(u"fleeDistance"_s, settings.fleeDistance, 0.0, 32.0);
    settings.shelterFromRain = reader.boolean(u"shelterFromRain"_s, settings.shelterFromRain);
    settings.trading = QcTradeParser::readTradingSettings(reader);
    settings.breeding = readBreeding(reader);
    if (reader.ok() && (settings.unemployed.isEmpty() || !settings.look.isSet())) {
        reader.fail(u"needs 'unemployed' and a 'look'"_s);
    }
    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return settings;
}

QcResult<QcVillagerProfession> parseProfession(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"profession"_s, {}};
    QcVillagerProfession profession;
    profession.name = reader.name(u"name"_s, packNamespace);
    if (profession.name.isEmpty()) {
        return qcError(reader.ok() ? u"profession: 'name' is required"_s : reader.error);
    }

    reader.context = u"profession '%1'"_s.arg(profession.name);
    QcContentFields::checkFormatVersion(QcContentFields::villageFormatVersion, reader);
    refuseMoved({{u"workstation"_s, u"'workstations' (the blocks the profession works at)"_s}}, reader);
    profession.translationKey = reader.string(u"translation"_s, QcContentFields::translationKey(u"profession"_s, profession.name));
    profession.workstationBlocks = readWorkstationBlocks(packNamespace, reader);
    profession.workstation = profession.workstationBlocks.isEmpty() ? QString() : profession.name;
    profession.hat = reader.name(u"hat"_s, packNamespace);
    profession.tendsCrops = reader.boolean(u"tendsCrops"_s, false);
    profession.layers = readLayers(json, profession.name, packNamespace, reader);
    if (json.contains(u"routine"_s)) {
        profession.routine = readRoutine(json, reader);
    }
    profession.offersPerLevel = reader.integer(u"offersPerLevel"_s, 0, 1, 8);
    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return profession;
}

} // namespace QcVillagerParser
