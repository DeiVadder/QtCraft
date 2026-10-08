#include "QcPartGenerator.h"

#include "QcContentParser.h"
#include "QcContentRegistry.h"

#include <QJsonArray>

#include <QSet>

using namespace Qt::StringLiterals;

namespace QcPartGenerator {

namespace {

QString localPart(const QString &name)
{
    return name.mid(name.indexOf(u':') + 1);
}

QString namespaceOf(const QString &name)
{
    return name.left(name.indexOf(u':'));
}

} // namespace

QString unsuitableBase(const QcContentRegistry &registry, const QString &block)
{
    const std::optional<QcBlockId> id = registry.blockId(block);
    if (!id) {
        return u"unknown block"_s;
    }
    const QcBlockDefinition &definition = registry.block(*id);
    if (definition.liquid || definition.renderLayer != QcRenderLayer::Opaque || !definition.solid || !definition.opaque) {
        return u"not an opaque solid block"_s;
    }
    if (!definition.properties.isEmpty() || !registry.state(*id).fullCube) {
        return u"not a plain full cube"_s;
    }
    if (definition.machine.isMachine() || definition.opensCrafting || definition.containerSlots > 0) {
        return u"a working block"_s;
    }
    if (definition.hardness < 0.0) {
        return u"unbreakable"_s;
    }
    if (definition.lightLevel > 0) {
        return u"emits light"_s;
    }
    if (definition.sapling.isSapling()) {
        return u"a sapling"_s;
    }
    return {};
}

QStringList generate(QcContentRegistry &registry)
{
    QStringList warnings;
    QSet<QString> excluded;
    for (const QcMaterialDefinition &material : registry.materials()) {
        if (material.excluded) {
            excluded.unite(QSet<QString>(material.excludedBlocks.begin(), material.excludedBlocks.end()));
        }
    }
    // Copies: registering blocks and recipes below must not invalidate what is iterated.
    const QList<QcMaterialDefinition> materials = registry.materials();
    const QList<QcPartFamily> families = registry.partFamilies();
    for (const QcMaterialDefinition &material : materials) {
        if (material.excluded || excluded.contains(material.block)) {
            continue;
        }
        if (const QString reason = unsuitableBase(registry, material.block); !reason.isEmpty()) {
            warnings.append(u"material %1: base block %2 cannot have parts (%3)"_s.arg(material.name, material.block, reason));
            continue;
        }
        const std::optional<QcBlockId> baseId = registry.blockId(material.block);
        if (!baseId) {
            continue;
        }
        const QcBlockDefinition base = registry.block(*baseId);
        const QString materialNamespace = namespaceOf(material.name);
        for (const QcPartFamily &family : families) {
            if (!family.kinds.contains(material.kind) || material.exclude.contains(family.name)) {
                continue;
            }
            const QString name = u"%1:%2_%3"_s.arg(materialNamespace, localPart(material.name), family.suffix);
            if (const std::optional<QcBlockId> existing = registry.blockId(name); existing && registry.block(*existing).partOf.isEmpty()) {
                continue;
            }
            const QString translationKey = u"block.%1.%2"_s.arg(materialNamespace, localPart(name).replace(u'/', u'.'));
            QJsonObject json = family.block;
            json.insert(u"name"_s, name);
            json.insert(u"translation"_s, translationKey);
            json.insert(u"textures"_s, QJsonObject{{u"east"_s, base.texture(QcBlockFace::East)},
                                                   {u"west"_s, base.texture(QcBlockFace::West)},
                                                   {u"top"_s, base.texture(QcBlockFace::Up)},
                                                   {u"bottom"_s, base.texture(QcBlockFace::Down)},
                                                   {u"south"_s, base.texture(QcBlockFace::South)},
                                                   {u"north"_s, base.texture(QcBlockFace::North)}});
            json.insert(u"hardness"_s, base.hardness * family.hardnessFactor);
            json.insert(u"tool"_s, base.tool);
            json.insert(u"toolLevel"_s, base.toolLevel);
            if (!base.soundMaterial.isEmpty()) {
                json.insert(u"sound"_s, base.soundMaterial);
            }
            json.insert(u"fuelSeconds"_s, base.fuelSeconds * family.fuelFactor);
            json.insert(u"drops"_s, QJsonArray{QJsonObject{{u"item"_s, name}}});
            QcResult<QcBlockDefinition> block = QcContentParser::parseBlock(json, materialNamespace);
            if (!block) {
                warnings.append(u"material %1, part %2: %3"_s.arg(material.name, family.name, block.error()));
                continue;
            }
            block->partOf = material.name + u'/' + family.name;
            if (const QcResult<QcBlockId> id = registry.registerBlock(std::move(*block)); !id) {
                warnings.append(u"material %1, part %2: %3"_s.arg(material.name, family.name, id.error()));
                continue;
            }
            const QcResult<QcRecipeDefinition> recipe =
                QcContentParser::parseRecipe(QcContentParser::partRecipeJson(family, material.block, name), materialNamespace);
            if (!recipe) {
                warnings.append(u"material %1, part %2: %3"_s.arg(material.name, family.name, recipe.error()));
            } else if (!registry.recipeIndex(recipe->name)) {
                // A recipe a pack defines under the part's name wins over the generated one.
                registry.registerRecipe(*recipe);
            }
            for (const auto &[language, format] : family.translations) {
                if (!registry.hasTranslation(translationKey, language)) {
                    registry.addTranslations(language, {{translationKey, format.arg(registry.translate(material.translationKey, language))}});
                }
            }
        }
    }
    return warnings;
}

} // namespace QcPartGenerator
