// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/items/QcPotionParser.h"

#include "common/content/registry/QcContentFields.h"
#include "common/content/registry/QcJsonFieldReader.h"

#include <QJsonArray>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

// At most this many effects in a potion.
constexpr qsizetype maxEffects = 8;

[[nodiscard]] std::optional<QcPotionForm> formNamed(const QString &name)
{
    const auto form = std::ranges::find_if(qcPotionForms, [&name](QcPotionForm candidate) { return QcPotionParser::formName(candidate) == name; });
    return form == qcPotionForms.end() ? std::nullopt : std::optional(*form);
}

// The forms of "forms"; every form when the field is missing.
[[nodiscard]] QList<QcPotionForm> parseForms(const QJsonObject &json, QcJsonFieldReader &reader)
{
    const QJsonValue value = json.value(u"forms"_s);
    if (value.isUndefined()) {
        return {qcPotionForms.begin(), qcPotionForms.end()};
    }
    if (!value.isArray() || value.toArray().isEmpty()) {
        reader.fail(u"'forms' must be a list of drink, splash and lingering"_s);
        return {};
    }

    QList<QcPotionForm> forms;
    const QJsonArray entries = value.toArray();
    for (const QJsonValueConstRef entry : entries) {
        const std::optional<QcPotionForm> form = formNamed(entry.toString());
        if (!form || forms.contains(*form)) {
            reader.fail(u"invalid 'forms' entry"_s);
            return {};
        }
        forms.append(*form);
    }
    return forms;
}

} // namespace

namespace QcPotionParser {

QString formName(QcPotionForm form)
{
    switch (form) {
    case QcPotionForm::Splash:
        return u"splash"_s;
    case QcPotionForm::Lingering:
        return u"lingering"_s;
    case QcPotionForm::Drink:
        break;
    }
    return u"drink"_s;
}

QList<QcPotionEffect> parseEffects(const QJsonObject &json, QcJsonFieldReader &reader, const QString &packNamespace)
{
    const QJsonValue value = json.value(u"effects"_s);
    if (value.isUndefined()) {
        return {};
    }
    if (!value.isArray() || value.toArray().size() > maxEffects) {
        reader.fail(u"'effects' must be a list of at most 8 effects"_s);
        return {};
    }

    QList<QcPotionEffect> effects;
    const QJsonArray entries = value.toArray();
    for (const QJsonValueConstRef entry : entries) {
        QcJsonFieldReader nested{entry.toObject(), reader.context, {}};
        QcPotionEffect effect;
        effect.effect = nested.name(u"effect"_s, packNamespace);
        effect.seconds = nested.number(u"seconds"_s, effect.seconds, 0.0, QcPotionEffect::maxSeconds);
        effect.level = nested.integer(u"level"_s, effect.level, 1, QcPotionEffect::maxLevel);
        if (!nested.ok() || effect.effect.isEmpty()) {
            reader.fail(nested.ok() ? u"an effect needs 'effect'"_s : nested.error);
            return {};
        }
        effects.append(effect);
    }
    return effects;
}

QcResult<QcPotionDefinition> parsePotion(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"potion"_s, {}};
    QcPotionDefinition potion;
    potion.name = reader.name(u"name"_s, packNamespace);
    if (potion.name.isEmpty()) {
        return qcError(reader.ok() ? u"potion: 'name' is required"_s : reader.error);
    }

    reader.context = u"potion '%1'"_s.arg(potion.name);
    potion.translationKey = reader.string(u"translation"_s, QcContentFields::translationKey(u"potion"_s, potion.name));
    potion.colour = reader.colour(u"colour"_s);
    potion.effects = parseEffects(json, reader, packNamespace);
    potion.forms = parseForms(json, reader);
    potion.item = reader.name(u"item"_s, packNamespace);
    potion.variantOf = reader.name(u"variantOf"_s, packNamespace);
    potion.variant = reader.string(u"variant"_s, {});
    if (reader.ok() && potion.variantOf.isEmpty() != potion.variant.isEmpty()) {
        reader.fail(u"'variantOf' and 'variant' go together"_s);
    }
    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return potion;
}

QcResult<QcBrewingRecipe> parseBrewingRecipe(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"brewing recipe"_s, {}};
    QcBrewingRecipe recipe;
    recipe.name = reader.name(u"name"_s, packNamespace);
    if (recipe.name.isEmpty()) {
        return qcError(reader.ok() ? u"brewing recipe: 'name' is required"_s : reader.error);
    }

    reader.context = u"brewing recipe '%1'"_s.arg(recipe.name);
    recipe.ingredient = reader.name(u"ingredient"_s, packNamespace);
    recipe.base = reader.name(u"base"_s, packNamespace);
    recipe.result = reader.name(u"result"_s, packNamespace);
    recipe.variant = reader.string(u"variant"_s, {});
    const QString form = reader.string(u"form"_s, {});
    const std::optional<QcPotionForm> changed = formNamed(form);
    if (reader.ok() && (recipe.ingredient.isEmpty() || (!form.isEmpty() && (!changed || *changed == QcPotionForm::Drink)))) {
        reader.fail(u"it needs an 'ingredient', and 'form' is splash or lingering"_s);
    }
    recipe.form = changed.value_or(QcPotionForm::Drink);
    const int ways = (!recipe.base.isEmpty() && !recipe.result.isEmpty() ? 1 : 0) + (!recipe.variant.isEmpty() ? 1 : 0) + (!form.isEmpty() ? 1 : 0);
    if (reader.ok() && (ways != 1 || recipe.base.isEmpty() != recipe.result.isEmpty())) {
        reader.fail(u"it has either 'base' and 'result', or 'variant', or 'form'"_s);
    }
    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return recipe;
}

} // namespace QcPotionParser
