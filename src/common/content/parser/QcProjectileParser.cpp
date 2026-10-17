#include "common/content/parser/QcProjectileParser.h"

#include "common/content/parser/QcJsonFieldReader.h"

#include <QJsonArray>

using namespace Qt::StringLiterals;

namespace {

// One entry of "onHit". A type no row of QcHitEffect::types names is kept as written; the
// content's finish leaves it out with a warning (QcContentRegistry::resolveProjectiles). A damage
// entry without an amount deals the kind's `damage`.
QcHitEffect parseEffect(const QJsonObject &json, const QString &packNamespace, double damage, QcJsonFieldReader &reader)
{
    QcJsonFieldReader nested{json, reader.context + u" onHit"_s, {}};
    QcHitEffect effect;
    effect.type = nested.string(u"type"_s);
    effect.fields = json;
    if (effect.type.isEmpty() && nested.ok()) {
        nested.fail(u"every entry needs a 'type'"_s);
    }
    if (const std::optional<QcHitEffect::Type> type = QcHitEffect::typeNamed(effect.type); type && *type->numberKey) {
        const double fallback = effect.is(QcHitEffect::damage) ? damage : type->fallback;
        effect.number = nested.number(QString::fromLatin1(type->numberKey), fallback, type->low, type->high);
    }
    effect.sound = nested.name(u"sound"_s, packNamespace);
    reader.adopt(nested);
    return effect;
}

// The "onHit" list; without one the arrow's damage (the kind's `damage`) and knockback.
QList<QcHitEffect> parseOnHit(const QString &packNamespace, double damage, QcJsonFieldReader &reader)
{
    const QJsonValue value = reader.json.value(u"onHit"_s);
    if (value.isUndefined()) {
        return {QcHitEffect{QString::fromLatin1(QcHitEffect::damage), damage, {}, {}},
                QcHitEffect{QString::fromLatin1(QcHitEffect::knockback), 1.0, {}, {}}};
    }

    const QJsonArray list = value.toArray();
    if (!value.isArray() || list.size() > 16) {
        reader.fail(u"'onHit' must be an array of at most 16 effects"_s);
        return {};
    }
    QList<QcHitEffect> effects;
    for (const auto &entry : list) {
        if (!entry.isObject()) {
            reader.fail(u"every 'onHit' entry must be an object"_s);
            return {};
        }
        effects.append(parseEffect(entry.toObject(), packNamespace, damage, reader));
    }
    return effects;
}

} // namespace

namespace QcProjectileParser {

[[nodiscard]] QcResult<QcProjectileDefinition> parseProjectile(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"projectile"_s, {}};
    QcProjectileDefinition projectile;
    projectile.name = reader.name(u"name"_s, packNamespace);
    if (projectile.name.isEmpty()) {
        return qcError(reader.ok() ? u"projectile: 'name' is required"_s : reader.error);
    }

    reader.context = u"projectile '%1'"_s.arg(projectile.name);
    projectile.item = reader.name(u"item"_s, packNamespace);
    if (projectile.item.isEmpty() && reader.ok()) {
        reader.fail(u"'item' is required"_s);
    }
    projectile.gravity = reader.number(u"gravity"_s, projectile.gravity, 0.0, 100.0);
    projectile.drag = reader.number(u"drag"_s, projectile.drag, 0.0, 50.0);
    projectile.waterDrag = reader.number(u"waterDrag"_s, projectile.waterDrag, 0.0, 50.0);
    projectile.damage = reader.number(u"damage"_s, projectile.damage, 0.0, 1000.0);
    projectile.size = reader.number(u"size"_s, projectile.size, 0.05, 2.0);
    projectile.despawnSeconds = reader.number(u"despawnSeconds"_s, projectile.despawnSeconds, 0.0, 3600.0);
    projectile.flightSeconds = reader.number(u"flightSeconds"_s, projectile.flightSeconds, 1.0, 3600.0);
    projectile.sticks = reader.boolean(u"sticks"_s, projectile.sticks);
    projectile.onHit = parseOnHit(packNamespace, projectile.damage, reader);
    // The damage of the hit is that of the damage effects.
    projectile.damage = 0.0;
    for (const QcHitEffect &effect : std::as_const(projectile.onHit)) {
        projectile.damage += effect.is(QcHitEffect::damage) ? effect.number : 0.0;
    }
    projectile.hitSound = reader.name(u"hitSound"_s, packNamespace);
    projectile.stickSound = reader.name(u"stickSound"_s, packNamespace);
    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return projectile;
}

} // namespace QcProjectileParser
