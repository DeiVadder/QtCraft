// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/items/QcPotionGenerator.h"

#include "common/content/items/QcPotionParser.h"
#include "common/content/registry/QcContentParser.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/physics/QcPhysics.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// A lingering potion lasts a quarter of the drink's time, as the reference's cloud gives it.
constexpr double lingeringShare = 0.25;
// How an instant effect travels in a hit entry, which needs a duration.
constexpr double instantSeconds = 1.0;
// A tipped arrow gives the eighth part of the drink's time, at least one tick (decision E-1101).
constexpr double tippedShare = 0.125;
// The arrows one potion tips, around it in the crafting grid.
constexpr int tippedArrows = 8;
// The form a tipped arrow is made from: the reference's lingering potion needs the End, the splash
// potion is the last step there is until then (decision E-1100).
constexpr QcPotionForm tippingForm = QcPotionForm::Splash;
// The form's name in the texts of a tipped arrow.
constexpr auto tippedForm = "tipped";
// The plain arrow: its flight and hit go to every tipped arrow, its tag makes them ammunition.
constexpr auto plainArrow = "qtcraft:arrow";
constexpr auto arrowTag = "qtcraft:arrows";
// The shaft of a tipped arrow when the plain arrow's texture gives none (a pack's picture).
constexpr auto arrowShaft = "#8a6a44";
// The bottle's glass and the liquid of a potion without effects (the reference's water colour).
constexpr auto bottleGlass = "#c8dce6";
constexpr auto splashGlass = "#d9cfe8";
constexpr auto lingeringGlass = "#e6dcc0";
constexpr quint32 waterColour = 0x385dc6;

QString namespaceOf(const QString &name)
{
    return name.left(name.indexOf(u':'));
}

QString localPart(const QString &name)
{
    return name.mid(name.indexOf(u':') + 1);
}

// The qualified item name of `form` of `potion`.
QString itemName(const QcPotionDefinition &potion, QcPotionForm form)
{
    if (form == QcPotionForm::Drink && !potion.item.isEmpty()) {
        return potion.item;
    }
    const QString infix = form == QcPotionForm::Drink ? QString() : QcPotionParser::formName(form) + u'_';
    return u"%1:%2_%3potion"_s.arg(namespaceOf(potion.name), localPart(potion.name), infix);
}

// What the potion is called in `language`; a variant without text of its own says what it is.
QString potionText(const QcContentRegistry &registry, const QcPotionDefinition &potion, const QString &language)
{
    if (registry.translations().has(potion.translationKey, language) || potion.variantOf.isEmpty()) {
        return registry.translations().translate(potion.translationKey, language);
    }
    const QcPotionDefinition *base = registry.potionCatalog().potion(potion.variantOf);
    const QString text = base ? potionText(registry, *base, language) : QString();
    return registry.translations().translate(u"potion.variant.%1"_s.arg(potion.variant), language).arg(text);
}

// Gives the item of the form named `form` its display name in every language with potion texts,
// unless a pack wrote one; the others fall back to the default language like every other text.
void addTexts(QcContentRegistry &registry, const QcPotionDefinition &potion, const QString &form, const QString &key)
{
    const QString formKey = u"potion.form.%1"_s.arg(form);
    const QStringList languages = registry.translations().languages();
    for (const QString &language : languages) {
        if (registry.translations().has(formKey, language) && !registry.translations().has(key, language)) {
            registry.translations().add(language, {{key, registry.translations().translate(formKey, language).arg(potionText(registry, potion, language))}});
        }
    }
}

// The colour of the liquid as "#rrggbb".
QString liquidColour(const QcContentRegistry &registry, const QcPotionDefinition &potion)
{
    const QcEffectDefinition *effect = potion.effects.isEmpty() ? nullptr : registry.effect(potion.effects.constFirst().effect);
    const quint32 colour = potion.colour.value_or(effect ? effect->colour : waterColour);
    return u"#%1"_s.arg(colour, 6, 16, QChar{u'0'});
}

// Gives the item `name` of `potion` the texture `pattern`, with `primary` and the colour of the
// liquid, unless a pack has one.
void addTexture(QcContentRegistry &registry, const QcPotionDefinition &potion, const QString &name, const QString &pattern, const QString &primary)
{
    if (registry.assets().texture(name)) {
        return;
    }
    const QJsonObject procedural{{u"name"_s, name},
                                 {u"pattern"_s, pattern},
                                 {u"primary"_s, primary},
                                 {u"secondary"_s, liquidColour(registry, potion)},
                                 {u"variation"_s, 0.06},
                                 {u"seed"_s, static_cast<int>(qChecksum(localPart(name).toUtf8()) % 9973U)}};
    registry.assets().registerTexture({name, procedural, {}});
}

QJsonArray drinkEffects(const QcPotionDefinition &potion)
{
    QJsonArray effects;
    for (const QcPotionEffect &effect : potion.effects) {
        effects.append(QJsonObject{{u"effect"_s, effect.effect}, {u"seconds"_s, effect.seconds}, {u"level"_s, effect.level}});
    }
    return effects;
}

// The item file of `form` of `potion`.
QJsonObject itemJson(const QcPotionDefinition &potion, QcPotionForm form, const QString &name, const QString &key)
{
    QJsonObject json{{u"name"_s, name},
                     {u"translation"_s, key},
                     {u"description"_s, u"potion.form.%1.description"_s.arg(QcPotionParser::formName(form))},
                     {u"texture"_s, name},
                     {u"maxStack"_s, 1}};
    if (form == QcPotionForm::Drink) {
        json.insert(u"drink"_s, QJsonObject{{u"effects"_s, drinkEffects(potion)}});
        json.insert(u"emptiesTo"_s, u"qtcraft:glass_bottle"_s);
        return json;
    }
    json.insert(u"projectile"_s, name);
    json.insert(u"dispense"_s, u"shoot"_s);
    json.insert(u"throwable"_s, QJsonObject{{u"speed"_s, 15.0}, {u"cooldownSeconds"_s, 0.5}, {u"inaccuracyDegrees"_s, 1.0}});
    return json;
}

// The projectile file of a thrown `form` of `potion`, as the splash potions of decision E-624.
QJsonObject projectileJson(const QcContentRegistry &registry, const QcPotionDefinition &potion, QcPotionForm form, const QString &name)
{
    QJsonArray onHit{QJsonObject{{u"type"_s, u"splash"_s}, {u"radius"_s, 4.0}}};
    const double share = form == QcPotionForm::Lingering ? lingeringShare : 1.0;
    for (const QcPotionEffect &effect : potion.effects) {
        const QcEffectDefinition *kind = registry.effect(effect.effect);
        const double seconds = kind && kind->instant ? instantSeconds : effect.seconds * share;
        onHit.append(QJsonObject{{u"type"_s, u"effect"_s}, {u"effect"_s, effect.effect}, {u"seconds"_s, seconds}, {u"level"_s, effect.level}});
    }
    onHit.append(QJsonObject{{u"type"_s, u"break"_s}, {u"sound"_s, u"block.glass.break"_s}});
    return {{u"name"_s, name},
            {u"item"_s, name},
            {u"gravity"_s, 20.0},
            {u"drag"_s, 0.2},
            {u"waterDrag"_s, 10.0},
            {u"onHit"_s, onHit},
            {u"size"_s, 0.25},
            {u"flightSeconds"_s, 60.0},
            {u"sticks"_s, false}};
}

// Registers the item of `form` of `potion` unless a pack defined it; the error is the warning.
QcResult<QcItemId> addItem(QcContentRegistry &registry, const QcPotionDefinition &potion, QcPotionForm form)
{
    const QString name = itemName(potion, form);
    if (const std::optional<QcItemId> existing = registry.itemId(name)) {
        return *existing;
    }

    const QString packNamespace = namespaceOf(name);
    const QString key = u"item.%1.%2"_s.arg(packNamespace, localPart(name));
    if (form != QcPotionForm::Drink) {
        QcResult<QcProjectileDefinition> projectile = QcContentParser::parseProjectile(projectileJson(registry, potion, form, name), packNamespace);
        if (!projectile) {
            return qcError(projectile.error());
        }
        registry.registerProjectile(std::move(*projectile));
    }
    QcResult<QcItemDefinition> item = QcContentParser::parseItem(itemJson(potion, form, name, key), packNamespace);
    if (!item) {
        return qcError(item.error());
    }
    const auto glass = form == QcPotionForm::Drink ? bottleGlass : form == QcPotionForm::Splash ? splashGlass : lingeringGlass;
    addTexture(registry, potion, name, u"bottle"_s, QString::fromLatin1(glass));
    addTexts(registry, potion, QcPotionParser::formName(form), key);
    return registry.registerItem(std::move(*item));
}

// How many seconds of `seconds` a tipped arrow gives: whole ticks, rounded down as the reference's
// integer division does, at least one.
double tippedSeconds(double seconds)
{
    return std::max(std::floor(seconds * tippedShare * QcPhysics::tickRate), 1.0) * QcPhysics::tickDuration;
}

// The hit effect of `effect` on a tipped arrow; an instant effect keeps its full strength.
QcHitEffect tippedEffect(const QcContentRegistry &registry, const QcPotionEffect &effect)
{
    const QcEffectDefinition *kind = registry.effect(effect.effect);
    QcHitEffect hit;
    hit.type = QString::fromLatin1(QcHitEffect::effect);
    hit.number = kind && kind->instant ? instantSeconds : tippedSeconds(effect.seconds);
    hit.subject = effect.effect;
    hit.level = effect.level;
    return hit;
}

// The item file of the tipped arrow `name`: the plain arrow's ammunition with a look of its own.
QJsonObject arrowItemJson(const QString &name, const QString &key)
{
    return {{u"name"_s, name},
            {u"translation"_s, key},
            {u"description"_s, u"potion.form.%1.description"_s.arg(QString::fromLatin1(tippedForm))},
            {u"texture"_s, name},
            {u"tags"_s, QJsonArray{QString::fromLatin1(arrowTag)}},
            {u"projectile"_s, name},
            {u"dispense"_s, u"shoot"_s}};
}

// The recipe of the tipped arrow `name`: eight arrows around `bottle`.
QJsonObject arrowRecipeJson(const QString &name, const QString &bottle)
{
    return {{u"name"_s, name},
            {u"type"_s, u"shaped"_s},
            {u"pattern"_s, QJsonArray{u"AAA"_s, u"APA"_s, u"AAA"_s}},
            {u"key"_s, QJsonObject{{u"A"_s, QString::fromLatin1(plainArrow)}, {u"P"_s, bottle}}},
            {u"result"_s, QJsonObject{{u"item"_s, name}, {u"count"_s, tippedArrows}}}};
}

// The shaft of the plain arrow's texture.
QString shaftColour(const QcContentRegistry &registry)
{
    const QcTextureDefinition *plain = registry.assets().texture(QString::fromLatin1(plainArrow));
    return plain ? plain->procedural.value(u"primary"_s).toString(QString::fromLatin1(arrowShaft)) : QString::fromLatin1(arrowShaft);
}

// Registers the tipped arrow of `potion`: its item unless a pack defined it, then what a pack did
// not write of its projectile (what the plain arrow does and the potion's effects), recipe and
// texture. The error is the warning.
QcResult<QcItemId> addArrow(QcContentRegistry &registry, const QcPotionDefinition &potion, const QcProjectileDefinition &plain)
{
    const QString name = u"%1:%2_tipped_arrow"_s.arg(namespaceOf(potion.name), localPart(potion.name));
    if (const std::optional<QcItemId> existing = registry.itemId(name)) {
        return *existing;
    }

    const QString packNamespace = namespaceOf(name);
    const QString key = u"item.%1.%2"_s.arg(packNamespace, localPart(name));
    QcResult<QcItemDefinition> item = QcContentParser::parseItem(arrowItemJson(name, key), packNamespace);
    if (!item) {
        return qcError(item.error());
    }
    QcResult<QcRecipeDefinition> recipe = QcContentParser::parseRecipe(arrowRecipeJson(name, itemName(potion, tippingForm)), packNamespace);
    if (!recipe) {
        return qcError(recipe.error());
    }

    QcProjectileDefinition projectile = plain;
    projectile.name = name;
    projectile.item = name;
    for (const QcPotionEffect &effect : potion.effects) {
        projectile.onHit.append(tippedEffect(registry, effect));
    }
    if (!registry.projectile(name)) {
        registry.registerProjectile(std::move(projectile));
    }
    if (!registry.recipeIndex(recipe->name)) {
        registry.registerRecipe(std::move(*recipe));
    }
    addTexture(registry, potion, name, u"tipped_arrow"_s, shaftColour(registry));
    addTexts(registry, potion, QString::fromLatin1(tippedForm), key);
    return registry.registerItem(std::move(*item));
}

// Gives the potions with an effect and the form arrows are made from their tipped arrow; content
// without the plain arrow has none.
QStringList addArrows(QcContentRegistry &registry, const QList<QcPotionDefinition> &potions)
{
    const QcProjectileDefinition *found = registry.projectile(QString::fromLatin1(plainArrow));
    if (!found) {
        return {};
    }

    QStringList warnings;
    // A copy: registering projectiles below may move the plain one.
    const QcProjectileDefinition plain = *found;
    for (const QcPotionDefinition &potion : potions) {
        if (potion.effects.isEmpty() || !potion.hasForm(tippingForm)) {
            continue;
        }
        if (const QcResult<QcItemId> arrow = addArrow(registry, potion, plain); !arrow) {
            warnings.append(u"potion '%1': %2"_s.arg(potion.name, arrow.error()));
        }
    }
    return warnings;
}

} // namespace

namespace QcPotionGenerator {

QStringList generate(QcContentRegistry &registry)
{
    QStringList warnings;
    // A copy: the catalogue is written to while its potions are walked through.
    const QList<QcPotionDefinition> potions = registry.potionCatalog().potions();
    for (const QcPotionDefinition &potion : potions) {
        for (const QcPotionEffect &effect : potion.effects) {
            if (!registry.effect(effect.effect)) {
                warnings.append(u"potion '%1': unknown effect '%2'"_s.arg(potion.name, effect.effect));
            }
        }
        if (!potion.variantOf.isEmpty() && !registry.potionCatalog().potion(potion.variantOf)) {
            warnings.append(u"potion '%1': 'variantOf' names no potion"_s.arg(potion.name));
        }
        for (const QcPotionForm form : potion.forms) {
            const QcResult<QcItemId> item = addItem(registry, potion, form);
            if (item) {
                registry.potionCatalog().linkItem(*item, potion.name, form);
            } else {
                warnings.append(u"potion '%1': %2"_s.arg(potion.name, item.error()));
            }
        }
    }
    warnings.append(addArrows(registry, potions));
    return warnings;
}

} // namespace QcPotionGenerator
