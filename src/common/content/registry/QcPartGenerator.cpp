#include "common/content/registry/QcPartGenerator.h"

#include "common/content/parser/QcContentParser.h"
#include "common/content/parser/QcMaterialParser.h"
#include "common/content/registry/QcContentRegistry.h"

#include <QJsonArray>

#include <QSet>

#include <algorithm>

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

// Blocks that exclusion materials keep free of parts.
QSet<QString> excludedBlocks(const QcContentRegistry &registry)
{
    QSet<QString> excluded;
    for (const QcMaterialDefinition &material : registry.materials()) {
        if (material.excluded) {
            excluded.unite(QSet<QString>(material.excludedBlocks.begin(), material.excludedBlocks.end()));
        }
    }
    return excluded;
}

// One part to generate: a family applied to a material with its base block.
struct PartSource
{
    const QcMaterialDefinition &material;
    const QcPartFamily &family;
    const QcBlockDefinition &base;
};

// The block file of a part: the family's template with name, texts and the base block's looks and mining.
QJsonObject partBlockJson(const PartSource &source, const QString &name, const QString &translationKey)
{
    const QcPartFamily &family = source.family;
    const QcBlockDefinition &base = source.base;

    QJsonObject json = family.block;
    json.insert(u"name"_s, name);
    json.insert(u"translation"_s, translationKey);
    // All parts of a family share one handbook text unless the family's template names one.
    if (!json.contains(u"description"_s)) {
        json.insert(u"description"_s, u"part.%1.%2.description"_s.arg(namespaceOf(family.name), localPart(family.name)));
    }

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
    // Parts burn like their material (wooden stairs, wool carpets; decision E-392).
    if (base.fire.isFlammable()) {
        json.insert(u"flammable"_s, QJsonObject{{u"ignite"_s, base.fire.ignite}, {u"burn"_s, base.fire.burn}});
    }
    // Parts resist explosions like their material (decision E-600).
    if (base.blast.resistance >= 0.0) {
        json.insert(u"blastResistance"_s, base.blast.resistance);
    }
    json.insert(u"drops"_s, QJsonArray{QJsonObject{{u"item"_s, name}}});
    return json;
}

void addPartTranslations(QcContentRegistry &registry, const PartSource &source, const QString &translationKey)
{
    for (const auto &[language, format] : source.family.translations) {
        if (!registry.translations().has(translationKey, language)) {
            registry.translations().add(language, {{translationKey, format.arg(registry.translations().translate(source.material.translationKey, language))}});
        }
    }
}

// Registers the block, recipe and translations of one part; the error is the warning about a
// problem that left it or its recipe out.
[[nodiscard]] QcStatus generatePart(QcContentRegistry &registry, const PartSource &source)
{
    const QcMaterialDefinition &material = source.material;
    const QcPartFamily &family = source.family;
    const QString materialNamespace = namespaceOf(material.name);
    const QString name = u"%1:%2_%3"_s.arg(materialNamespace, localPart(material.name), family.suffix);
    if (const std::optional<QcBlockId> existing = registry.blockId(name); existing && registry.block(*existing).partOf.isEmpty()) {
        return {};
    }

    const QString translationKey = u"block.%1.%2"_s.arg(materialNamespace, localPart(name).replace(u'/', u'.'));
    QcResult<QcBlockDefinition> block = QcContentParser::parseBlock(partBlockJson(source, name, translationKey), materialNamespace);
    if (!block) {
        return qcError(u"material %1, part %2: %3"_s.arg(material.name, family.name, block.error()));
    }

    block->partOf = material.name + u'/' + family.name;
    if (const QcResult<QcBlockId> id = registry.registerBlock(std::move(*block)); !id) {
        return qcError(u"material %1, part %2: %3"_s.arg(material.name, family.name, id.error()));
    }

    addPartTranslations(registry, source, translationKey);
    QList<QJsonObject> recipes{QcContentParser::partRecipeJson(family, material.block, name)};
    recipes.append(QcMaterialParser::morePartRecipesJson(family, material.block, name));
    for (const QJsonObject &json : recipes) {
        const QcResult<QcRecipeDefinition> recipe = QcContentParser::parseRecipe(json, materialNamespace);
        if (!recipe) {
            return qcError(u"material %1, part %2: %3"_s.arg(material.name, family.name, recipe.error()));
        }
        // A recipe a pack defines under the same name wins over the generated one.
        if (!registry.recipeIndex(recipe->name)) {
            registry.registerRecipe(*recipe);
        }
    }
    return {};
}

// Whether the material has the other parts the family's recipes name (a vertical slab needs the
// slab, decision E-755).
bool hasSiblings(const QcContentRegistry &registry, const QcMaterialDefinition &material, const QcPartFamily &family)
{
    const QString prefix = u"%1:%2_"_s.arg(namespaceOf(material.name), localPart(material.name));
    const QStringList suffixes = QcMaterialParser::siblingSuffixes(family);
    return std::ranges::all_of(suffixes, [&](const QString &suffix) { return registry.blockId(prefix + suffix).has_value(); });
}

// The families in load order, those naming other parts after the ones they name.
QList<QcPartFamily> orderedFamilies(const QcContentRegistry &registry)
{
    QList<QcPartFamily> families;
    QList<QcPartFamily> later;
    for (const QcPartFamily &family : registry.partFamilies()) {
        (QcMaterialParser::siblingSuffixes(family).isEmpty() ? families : later).append(family);
    }
    return families + later;
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
    const QSet<QString> excluded = excludedBlocks(registry);
    // Copies: registering blocks and recipes below must not invalidate what is iterated.
    const QList<QcMaterialDefinition> materials = registry.materials();
    const QList<QcPartFamily> families = orderedFamilies(registry);

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
        for (const QcPartFamily &family : families) {
            if (!family.kinds.contains(material.kind) || material.exclude.contains(family.name) || !hasSiblings(registry, material, family)) {
                continue;
            }
            if (const QcStatus part = generatePart(registry, {material, family, base}); !part) {
                warnings.append(part.error());
            }
        }
    }
    return warnings;
}

} // namespace QcPartGenerator
