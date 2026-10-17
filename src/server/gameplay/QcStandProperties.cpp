#include "server/gameplay/QcStandProperties.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcNameText.h"
#include "common/gameplay/QcArmour.h"

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

QString qualified(const QString &name)
{
    return name.contains(u':') ? name : u"qtcraft:"_s + name;
}

[[nodiscard]] std::optional<bool> parseBool(const QString &value)
{
    if (value == u"true"_s || value == u"on"_s) {
        return true;
    }
    if (value == u"false"_s || value == u"off"_s) {
        return false;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<float> parseAngle(const QString &text)
{
    bool ok = false;
    const double value = text.toDouble(&ok);
    // Larger values are surely typing mistakes; within the range they are normalised.
    if (!ok || !std::isfinite(value) || std::abs(value) > 3600.0) {
        return std::nullopt;
    }
    return qcNormalizedAngle(float(value));
}

} // namespace

namespace QcStandProperties {

namespace {

bool applyFlag(QcArmourStand &stand, const QString &flag, const QString &value)
{
    const std::optional<bool> on = parseBool(value);
    if (!on) {
        return false;
    }

    QcStandPose &pose = stand.pose;
    bool *target = flag == u"arms"_s ? &pose.arms
                   : flag == u"plate"_s ? &pose.basePlate
                   : flag == u"small"_s ? &pose.small
                   : flag == u"showname"_s ? &pose.showName
                                           : nullptr;
    if (target) {
        *target = *on;
    }
    return target != nullptr;
}

bool applyPose(QcArmourStand &stand, const QStringList &args)
{
    const std::optional<QcStandPose::Part> part = QcStandPose::partFromName(args.value(1));
    if (!part || args.size() != 5) {
        return false;
    }

    const std::optional<float> x = parseAngle(args[2]);
    const std::optional<float> y = parseAngle(args[3]);
    const std::optional<float> z = parseAngle(args[4]);
    if (!x || !y || !z) {
        return false;
    }

    stand.pose.at(*part) = {*x, *y, *z};
    return true;
}

bool applyEquip(const QcContentRegistry &content, QcArmourStand &stand, const QString &name)
{
    const QList<QcItemStack> items = QcStandProperties::itemsNamed(content, name);
    for (const QcItemStack &stack : items) {
        if (const QcArmourDefinition *armour = QcArmour::of(content, stack.item)) {
            stand.equipment.at(armour->slot) = stack;
        } else {
            stand.held = stack;
        }
    }
    return !items.isEmpty();
}

bool applyCape(const QcContentRegistry &content, QcArmourStand &stand, const QString &name)
{
    if (name == u"none"_s) {
        stand.cape.clear();
        return true;
    }

    const QcAppearanceDefinition *cape = content.appearance(qualified(name));
    if (!cape || cape->kind != QcAppearanceDefinition::Kind::Cape) {
        return false;
    }
    stand.cape = cape->name;
    return true;
}

} // namespace

QList<QcItemStack> itemsNamed(const QcContentRegistry &content, const QString &name)
{
    // A set wins over an item of the same name ("diamond" is the set, not the gem).
    QList<QcItemStack> set;
    for (qsizetype i = 0; i < content.items().size(); ++i) {
        const QcItemDefinition &item = content.items()[i];
        if (item.armour && item.armour->set == qualified(name)) {
            set.append({QcItemId(i + 1), 1, 0});
        }
    }
    if (!set.isEmpty()) {
        return set;
    }

    if (const std::optional<QcItemId> item = content.itemId(qualified(name))) {
        return {{*item, 1, 0}};
    }
    return {};
}

[[nodiscard]] bool apply(const QcContentRegistry &content, QcArmourStand &stand, const QStringList &args)
{
    const QString &property = args.value(0);
    if (property == u"pose"_s) {
        return applyPose(stand, args);
    }
    if (property == u"rotation"_s) {
        const std::optional<float> yaw = args.size() == 2 ? parseAngle(args[1]) : std::nullopt;
        if (yaw) {
            stand.yaw = *yaw;
        }
        return yaw.has_value();
    }
    if (property == u"name"_s) {
        stand.name = QcNameText::clean(args.mid(1).join(u' '), QcArmourStandManager::maxNameLength);
        return true;
    }
    if (property == u"equip"_s) {
        return args.size() == 2 && applyEquip(content, stand, args[1]);
    }
    if (property == u"cape"_s) {
        return args.size() == 2 && applyCape(content, stand, args[1]);
    }
    if (property == u"clear"_s && args.size() == 1) {
        stand.equipment = {};
        stand.held = {};
        stand.cape.clear();
        return true;
    }
    if (property == u"reset"_s && args.size() == 1) {
        stand.pose = QcStandPose();
        return true;
    }
    return args.size() == 2 && applyFlag(stand, property, args[1]);
}

bool applyLine(const QcContentRegistry &content, QcArmourStand &stand, const QString &line)
{
    return apply(content, stand, line.split(u' ', Qt::SkipEmptyParts));
}

QString standItem(const QcContentRegistry &content)
{
    const QList<QcItemDefinition> &items = content.items();
    const auto found = std::ranges::find_if(items, &QcItemDefinition::placesStand);
    return found == items.end() ? QString() : found->name;
}

} // namespace QcStandProperties
