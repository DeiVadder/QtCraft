// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/parser/QcMaterialParser.h"

#include "common/content/parser/QcBlockParser.h"
#include "common/content/parser/QcContentFields.h"
#include "common/content/parser/QcJsonFieldReader.h"
#include "common/content/parser/QcRecipeParser.h"
#include "common/content/registry/QcContentNames.h"

#include <QJsonArray>

#include <utility>

using namespace Qt::StringLiterals;

namespace {

[[nodiscard]] QStringList parseFamilyKinds(const QJsonObject &json, QcJsonFieldReader &reader)
{
    QStringList result;
    const QJsonArray kinds = json.value(u"kinds"_s).toArray();
    for (const auto &kind : kinds) {
        if (!kind.isString() || kind.toString().isEmpty() || kind.toString().size() > 32 || result.size() >= 16) {
            reader.fail(u"'kinds' must list at most 16 material kinds"_s);
            break;
        }
        result.append(kind.toString());
    }

    if (result.isEmpty()) {
        reader.fail(u"'kinds' needs at least one material kind"_s);
    }
    return result;
}

[[nodiscard]] QList<std::pair<QString, QString>> parseFamilyTranslations(const QJsonObject &json, QcJsonFieldReader &reader)
{
    QList<std::pair<QString, QString>> result;
    const QJsonObject translations = json.value(u"translations"_s).toObject();
    for (auto it = translations.constBegin(); it != translations.constEnd(); ++it) {
        if (!it.value().isString() || !it.value().toString().contains(u"%1"_s) || it.value().toString().size() > 128
            || it.key().size() > 16 || result.size() >= 64) {
            reader.fail(u"'translations' maps languages to texts with %1 for the material"_s);
            break;
        }
        result.append({it.key(), it.value().toString()});
    }
    return result;
}

// Check the templates once with a stand-in material, so broken families fail at loading.
[[nodiscard]] QcStatus checkFamilyTemplates(const QcPartFamily &family, const QString &context, const QString &packNamespace)
{
    QJsonObject sample = family.block;
    sample.insert(u"name"_s, u"qtcraft:sample_part"_s);
    sample.insert(u"textures"_s, QJsonObject{{u"all"_s, u"qtcraft:sample"_s}});

    if (const auto block = QcBlockParser::parseBlock(sample, packNamespace); !block) {
        return qcError(context + u" block: "_s + block.error());
    }

    const QString sampleName = u"qtcraft:sample_"_s + family.suffix;
    QList<QJsonObject> recipes{QcMaterialParser::partRecipeJson(family, u"qtcraft:sample"_s, sampleName)};
    recipes.append(QcMaterialParser::morePartRecipesJson(family, u"qtcraft:sample"_s, sampleName));
    for (const QJsonObject &recipeJson : recipes) {
        if (const auto recipe = QcRecipeParser::parseRecipe(recipeJson, packNamespace); !recipe) {
            return qcError(context + u" recipe: "_s + recipe.error());
        }
    }
    return {};
}

// The blocks an exclusion keeps out of every family ("excluded": true).
[[nodiscard]] QStringList parseExcludedBlocks(const QJsonObject &json, QcJsonFieldReader &reader, const QString &packNamespace)
{
    QStringList result;
    const QJsonArray blocks = json.value(u"blocks"_s).toArray();
    for (const auto &entry : blocks) {
        const QString name = entry.isString() ? QcContentNames::qualify(entry.toString(), packNamespace) : QString();
        if (name.isEmpty() || result.size() >= 1024) {
            reader.fail(u"'blocks' must list at most 1024 block names"_s);
            break;
        }
        result.append(name);
    }

    if (reader.ok() && result.isEmpty()) {
        reader.fail(u"an exclusion needs 'blocks'"_s);
    }
    return result;
}

// The part families a material leaves out ("exclude").
[[nodiscard]] QStringList parseExcludedFamilies(const QJsonObject &json, QcJsonFieldReader &reader, const QString &packNamespace)
{
    QStringList result;
    const QJsonArray exclude = json.value(u"exclude"_s).toArray();
    for (const auto &entry : exclude) {
        const QString family = entry.isString() ? QcContentNames::qualify(entry.toString(), packNamespace) : QString();
        if (family.isEmpty() || result.size() >= 64) {
            reader.fail(u"'exclude' must list at most 64 part families"_s);
            break;
        }
        result.append(family);
    }
    return result;
}

// One part a family's recipes are resolved for.
struct PartNames
{
    const QcPartFamily &family;
    const QString &baseBlock;
    const QString &partName;
};

// "@" is the base block, "@<suffix>" the material's part of that suffix; anything else stays.
QJsonValue resolved(const QJsonValue &value, const PartNames &part)
{
    const QString text = value.toString();
    if (text == u"@"_s) {
        return part.baseBlock;
    }
    if (text.size() > 1 && text.startsWith(u'@')) {
        const QString material = part.partName.chopped(part.family.suffix.size() + 1);
        return QString(material + u'_' + text.mid(1));
    }
    return value;
}

// A shaped recipe file named `name` from a family's recipe template; its result is the part unless
// the template names a "result".
QJsonObject recipeFrom(const QJsonObject &recipe, const PartNames &part, const QString &name)
{
    QJsonObject keys;
    const QJsonObject familyKeys = recipe.value(u"key"_s).toObject();
    for (auto it = familyKeys.constBegin(); it != familyKeys.constEnd(); ++it) {
        keys.insert(it.key(), resolved(it.value(), part));
    }

    const QJsonValue result = recipe.contains(u"result"_s) ? resolved(recipe.value(u"result"_s), part) : QJsonValue(part.partName);
    return QJsonObject{{u"name"_s, name},
                       {u"type"_s, u"shaped"_s},
                       {u"pattern"_s, recipe.value(u"pattern"_s)},
                       {u"key"_s, keys},
                       {u"result"_s, QJsonObject{{u"item"_s, result}, {u"count"_s, recipe.value(u"count"_s).toInt(1)}}}};
}

// The further recipes of a family ("recipes"): objects with a plain "name".
[[nodiscard]] QJsonArray parseMoreRecipes(const QJsonObject &json, QcJsonFieldReader &reader)
{
    const QJsonValue value = json.value(u"recipes"_s);
    if (value.isUndefined()) {
        return {};
    }

    QJsonArray recipes = value.toArray();
    bool valid = value.isArray() && recipes.size() <= QcPartFamily::maxMoreRecipes;
    for (const auto &entry : std::as_const(recipes)) {
        const QString name = entry.toObject().value(u"name"_s).toString();
        valid = valid && entry.isObject() && QcContentNames::isValidPart(name) && !name.contains(u'/') && name.size() <= 32;
    }
    if (!valid) {
        reader.fail(u"'recipes' must list at most %1 recipes, each with a plain 'name'"_s.arg(QcPartFamily::maxMoreRecipes));
        return {};
    }
    return recipes;
}

} // namespace

namespace QcMaterialParser {

[[nodiscard]] QcResult<QcPartFamily> parsePartFamily(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"part family"_s, {}};
    QcPartFamily family;
    family.name = reader.name(u"name"_s, packNamespace);
    if (family.name.isEmpty()) {
        return qcError(reader.ok() ? u"part family: 'name' is required"_s : reader.error);
    }

    reader.context = u"part family '%1'"_s.arg(family.name);
    family.suffix = reader.string(u"suffix"_s, QcContentFields::localPart(family.name));
    if (!QcContentNames::isValidPart(family.suffix) || family.suffix.contains(u'/') || family.suffix.size() > 32) {
        reader.fail(u"'suffix' must be a plain name of at most 32 characters"_s);
    }

    family.kinds = parseFamilyKinds(json, reader);
    family.hardnessFactor = reader.number(u"hardness"_s, 1.0, 0.0, 100.0);
    family.fuelFactor = reader.number(u"fuel"_s, 1.0, 0.0, 100.0);
    family.block = json.value(u"block"_s).toObject();
    family.recipe = json.value(u"recipe"_s).toObject();
    if (!json.value(u"block"_s).isObject() || !json.value(u"recipe"_s).isObject()) {
        reader.fail(u"'block' and 'recipe' objects are required"_s);
    }
    family.moreRecipes = parseMoreRecipes(json, reader);
    family.translations = parseFamilyTranslations(json, reader);
    // "stonecutter": {"count": 2, "kinds": ["stone"]} (decision E-784).
    reader.nestedObject(u"stonecutter"_s, [&family](const QJsonObject &object, QcJsonFieldReader &nested) {
        family.stonecutterCount = nested.integer(u"count"_s, 1, 1, 64);
        family.stonecutterKinds = parseFamilyKinds(object, nested);
    });

    if (!reader.ok()) {
        return qcError(reader.error);
    }

    if (const QcStatus templates = checkFamilyTemplates(family, reader.context, packNamespace); !templates) {
        return qcError(templates.error());
    }
    return family;
}

[[nodiscard]] QcResult<QcMaterialDefinition> parseMaterial(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"material"_s, {}};
    QcMaterialDefinition material;
    material.name = reader.name(u"name"_s, packNamespace);
    if (material.name.isEmpty()) {
        return qcError(reader.ok() ? u"material: 'name' is required"_s : reader.error);
    }

    reader.context = u"material '%1'"_s.arg(material.name);
    material.excluded = reader.boolean(u"excluded"_s, false);
    if (material.excluded) {
        material.excludedBlocks = parseExcludedBlocks(json, reader, packNamespace);
    } else {
        material.block = reader.name(u"block"_s, packNamespace);
        material.kind = reader.string(u"kind"_s);
        if (reader.ok() && (material.block.isEmpty() || material.kind.isEmpty() || material.kind.size() > 32)) {
            reader.fail(u"'block' and 'kind' are required"_s);
        }
        material.exclude = parseExcludedFamilies(json, reader, packNamespace);
    }
    material.translationKey = reader.string(u"translation"_s, QcContentFields::translationKey(u"material"_s, material.name));

    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return material;
}

QJsonObject partRecipeJson(const QcPartFamily &family, const QString &baseBlock, const QString &partName)
{
    return recipeFrom(family.recipe, {family, baseBlock, partName}, partName);
}

QList<QJsonObject> morePartRecipesJson(const QcPartFamily &family, const QString &baseBlock, const QString &partName)
{
    QList<QJsonObject> recipes;
    for (const auto &entry : family.moreRecipes) {
        const QJsonObject recipe = entry.toObject();
        recipes.append(recipeFrom(recipe, {family, baseBlock, partName}, partName + u'_' + recipe.value(u"name"_s).toString()));
    }
    return recipes;
}

QStringList siblingSuffixes(const QcPartFamily &family)
{
    QStringList suffixes;
    const auto collect = [&](const QJsonValue &value) {
        const QString text = value.toString();
        if (text.size() > 1 && text.startsWith(u'@') && text.mid(1) != family.suffix && !suffixes.contains(text.mid(1))) {
            suffixes.append(text.mid(1));
        }
    };

    QList<QJsonObject> recipes{family.recipe};
    for (const auto &entry : family.moreRecipes) {
        recipes.append(entry.toObject());
    }
    for (const QJsonObject &recipe : recipes) {
        const QJsonObject keys = recipe.value(u"key"_s).toObject();
        for (auto it = keys.constBegin(); it != keys.constEnd(); ++it) {
            collect(it.value());
        }
        collect(recipe.value(u"result"_s));
    }
    return suffixes;
}

} // namespace QcMaterialParser
