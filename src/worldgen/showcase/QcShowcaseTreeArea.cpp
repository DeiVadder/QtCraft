#include "worldgen/showcase/QcShowcaseTreeArea.h"

#include "worldgen/terrain/QcTreeShape.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

// Each tree has a plot this wide; its sapling stands this far south of the trunk.
constexpr int plotWidth = 8;
constexpr int treeZ = 4;
constexpr int saplingDistance = 7;
// Crown room above the tallest trunk.
constexpr int crownRoom = 4;

bool isTree(const QcFeatureDefinition &feature)
{
    return feature.type == QcFeatureDefinition::Type::Tree || feature.type == QcFeatureDefinition::Type::Column;
}

QcBlockId firstKnown(const QcContentRegistry &content, const QList<QString> &names, QcBlockId fallback)
{
    for (const QString &name : names) {
        if (const std::optional<QcBlockId> id = content.blockId(name)) {
            return *id;
        }
    }
    return fallback;
}

QcTreeShape::Params paramsOf(const QcContentRegistry &content, const QcFeatureDefinition &feature)
{
    QcTreeShape::Params params;
    params.type = feature.type;
    params.trunk = content.blockId(feature.trunkBlock).value_or(qcAirBlockId);
    params.leaves = content.blockId(feature.leavesBlock).value_or(qcAirBlockId);
    params.minHeight = feature.minHeight;
    params.maxHeight = feature.maxHeight;
    params.cone = feature.shape == u"cone"_s;
    return params;
}

void growTree(QcStructure &structure, const QcShowcaseContext &context, const QcFeatureDefinition &feature, int x)
{
    const QcTreeShape::Params params = paramsOf(context.content, feature);
    structure.set(x, 0, treeZ, firstKnown(context.content, feature.groundBlocks, context.ground));
    QcTreeShape::build(params, {x, 1, treeZ}, 0.5, 0, [&structure](QcBlockPos pos, QcBlockId block, bool onlyIntoAir) {
        if (!structure.contains(pos.x, pos.y, pos.z) || (onlyIntoAir && structure.block(pos.x, pos.y, pos.z))) {
            return;
        }
        structure.set(pos.x, pos.y, pos.z, block);
    });

    const QString trunk = context.content.blockId(feature.trunkBlock) ? context.content.block(params.trunk).translationKey : feature.name;
    const int top = QcTreeShape::height(params, 0.5) + 2;
    structure.addLabel({{x + 0.5, top + 1.5, treeZ + 0.5}, trunk, feature.name.mid(feature.name.indexOf(u':') + 1), false});
}

// The saplings that grow into `feature`, side by side south of the tree.
void plantSaplings(QcStructure &structure, const QcShowcaseContext &context, const QcFeatureDefinition &feature, int x)
{
    int offset = 0;
    for (qsizetype type = 0; type < context.content.blocks().size(); ++type) {
        const QcBlockDefinition &block = context.content.blocks()[type];
        if (block.sapling.feature != feature.name) {
            continue;
        }

        const int sx = x + offset;
        const int sz = treeZ + saplingDistance;
        const QcBlockId sapling = context.content.stateTable().firstState(quint32(type));
        structure.set(sx, 0, sz, firstKnown(context.content, block.sapling.plantOn, context.ground));
        structure.set(sx, 1, sz, sapling);
        structure.addGrowing({sx, 1, sz});
        structure.addLabel({{sx + 0.5, 2.5, sz + 0.5}, block.translationKey, {}, false});
        offset += 2;
    }
}

} // namespace

namespace QcShowcaseTreeArea {

QcShowcaseArea build(const QcShowcaseContext &context, QcBlockPos anchor)
{
    QList<const QcFeatureDefinition *> trees;
    int tallest = 1;
    for (const QcFeatureDefinition &feature : context.content.worldgen().features()) {
        if (isTree(feature)) {
            trees.append(&feature);
            tallest = std::max(tallest, feature.maxHeight);
        }
    }

    const int depth = treeZ + saplingDistance + 2;
    auto structure = std::make_shared<QcStructure>(u"showcase/trees"_s, std::max<int>(1, int(trees.size()) * plotWidth), tallest + crownRoom + 2,
                                                   depth);
    for (qsizetype i = 0; i < trees.size(); ++i) {
        const int x = int(i) * plotWidth + plotWidth / 2;
        growTree(*structure, context, *trees[i], x);
        plantSaplings(*structure, context, *trees[i], x);
    }

    const QcBlockPos origin{anchor.x, anchor.y, anchor.z - depth + 1};
    return {u"trees"_s, u"showcase.area.trees"_s, std::move(structure), origin, {}, {}};
}

} // namespace QcShowcaseTreeArea
