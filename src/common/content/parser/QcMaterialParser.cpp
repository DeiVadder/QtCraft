#include "common/content/parser/QcMaterialParser.h"

#include "common/content/parser/QcBlockParser.h"
#include "common/content/parser/QcContentFields.h"
#include "common/content/parser/QcJsonFieldReader.h"
#include "common/content/parser/QcRecipeParser.h"
#include "common/content/registry/QcContentNames.h"

#include <QJsonArray>

using namespace Qt::StringLiterals;

namespace {

void parseFamilyKinds(const QJsonObject &json, QcPartFamily &family, QcJsonFieldReader &reader)
{
    const QJsonArray kinds = json.value(u"kinds"_s).toArray();
    for (const auto &kind : kinds) {
        if (!kind.isString() || kind.toString().isEmpty() || kind.toString().size() > 32 || family.kinds.size() >= 16) {
            reader.fail(u"'kinds' must list at most 16 material kinds"_s);
            break;
        }
        family.kinds.append(kind.toString());
    }

    if (family.kinds.isEmpty()) {
        reader.fail(u"'kinds' needs at least one material kind"_s);
    }
}

void parseFamilyTranslations(const QJsonObject &json, QcPartFamily &family, QcJsonFieldReader &reader)
{
    const QJsonObject translations = json.value(u"translations"_s).toObject();
    for (auto it = translations.constBegin(); it != translations.constEnd(); ++it) {
        if (!it.value().isString() || !it.value().toString().contains(u"%1"_s) || it.value().toString().size() > 128
            || it.key().size() > 16 || family.translations.size() >= 64) {
            reader.fail(u"'translations' maps languages to texts with %1 for the material"_s);
            break;
        }
        family.translations.append({it.key(), it.value().toString()});
    }
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

    const QJsonObject recipeJson = QcMaterialParser::partRecipeJson(family, u"qtcraft:sample"_s, u"qtcraft:sample_part"_s);
    if (const auto recipe = QcRecipeParser::parseRecipe(recipeJson, packNamespace); !recipe) {
        return qcError(context + u" recipe: "_s + recipe.error());
    }
    return {};
}

void parseExclusion(const QJsonObject &json, QcMaterialDefinition &material, QcJsonFieldReader &reader, const QString &packNamespace)
{
    const QJsonArray blocks = json.value(u"blocks"_s).toArray();
    for (const auto &entry : blocks) {
        const QString name = entry.isString() ? QcContentNames::qualify(entry.toString(), packNamespace) : QString();
        if (name.isEmpty() || material.excludedBlocks.size() >= 1024) {
            reader.fail(u"'blocks' must list at most 1024 block names"_s);
            break;
        }
        material.excludedBlocks.append(name);
    }

    if (reader.ok() && material.excludedBlocks.isEmpty()) {
        reader.fail(u"an exclusion needs 'blocks'"_s);
    }
}

void parseMaterialBlock(const QJsonObject &json, QcMaterialDefinition &material, QcJsonFieldReader &reader,
                        const QString &packNamespace)
{
    material.block = reader.name(u"block"_s, packNamespace);
    material.kind = reader.string(u"kind"_s);
    if (reader.ok() && (material.block.isEmpty() || material.kind.isEmpty() || material.kind.size() > 32)) {
        reader.fail(u"'block' and 'kind' are required"_s);
    }

    const QJsonArray exclude = json.value(u"exclude"_s).toArray();
    for (const auto &entry : exclude) {
        const QString family = entry.isString() ? QcContentNames::qualify(entry.toString(), packNamespace) : QString();
        if (family.isEmpty() || material.exclude.size() >= 64) {
            reader.fail(u"'exclude' must list at most 64 part families"_s);
            break;
        }
        material.exclude.append(family);
    }
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

    parseFamilyKinds(json, family, reader);
    family.hardnessFactor = reader.number(u"hardness"_s, 1.0, 0.0, 100.0);
    family.fuelFactor = reader.number(u"fuel"_s, 1.0, 0.0, 100.0);
    family.block = json.value(u"block"_s).toObject();
    family.recipe = json.value(u"recipe"_s).toObject();
    if (!json.value(u"block"_s).isObject() || !json.value(u"recipe"_s).isObject()) {
        reader.fail(u"'block' and 'recipe' objects are required"_s);
    }
    parseFamilyTranslations(json, family, reader);

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
        parseExclusion(json, material, reader, packNamespace);
    } else {
        parseMaterialBlock(json, material, reader, packNamespace);
    }
    material.translationKey = reader.string(u"translation"_s, QcContentFields::translationKey(u"material"_s, material.name));

    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return material;
}

QJsonObject partRecipeJson(const QcPartFamily &family, const QString &baseBlock, const QString &partName)
{
    QJsonObject keys;
    const QJsonObject familyKeys = family.recipe.value(u"key"_s).toObject();
    for (auto it = familyKeys.constBegin(); it != familyKeys.constEnd(); ++it) {
        keys.insert(it.key(), it.value().toString() == u"@"_s ? QJsonValue(baseBlock) : it.value());
    }

    return QJsonObject{{u"name"_s, partName},
                       {u"type"_s, u"shaped"_s},
                       {u"pattern"_s, family.recipe.value(u"pattern"_s)},
                       {u"key"_s, keys},
                       {u"result"_s, QJsonObject{{u"item"_s, partName}, {u"count"_s, family.recipe.value(u"count"_s).toInt(1)}}}};
}

} // namespace QcMaterialParser
