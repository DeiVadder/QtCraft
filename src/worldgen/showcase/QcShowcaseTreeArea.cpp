// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/showcase/QcShowcaseTreeArea.h"

#include "worldgen/terrain/QcTreeShape.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

// Each tree has a plot at least this wide, wider for giant trees (decision E-822); its saplings
// stand this far south of the trunk, beyond the widest crown.
constexpr int plotWidth = 8;
constexpr int treeZ = 6;
constexpr int saplingDistance = 8;
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

void growTree(QcStructure &structure, const QcShowcaseContext &context, const QcFeatureDefinition &feature, int x)
{
    const QcTreeShape::Params params = QcTreeShape::paramsOf(context.content, feature);
    const int width = params.giant() ? 2 : 1;
    for (int dx = 0; dx < width; ++dx) {
        for (int dz = 0; dz < width; ++dz) {
            structure.set(x + dx, 0, treeZ + dz, firstKnown(context.content, feature.groundBlocks, context.ground));
        }
    }
    QcTreeShape::build(params, {x, 1, treeZ}, 0.5, 0, [&structure](QcBlockPos pos, QcBlockId block, bool onlyIntoAir) {
        if (!structure.contains(pos.x, pos.y, pos.z) || (onlyIntoAir && structure.block(pos.x, pos.y, pos.z))) {
            return false;
        }
        structure.set(pos.x, pos.y, pos.z, block);
        return true;
    });

    const QString trunk = context.content.blockId(feature.trunkBlock) ? context.content.block(params.trunk).translationKey : feature.name;
    const int top = QcTreeShape::height(params, 0.5) + 2;
    structure.addLabel({{x + 0.5, top + 1.5, treeZ + 0.5}, trunk, feature.name.mid(feature.name.indexOf(u':') + 1), false});
}

// The saplings that grow into `feature`, side by side south of the tree: one each, or four in a
// square for a giant tree.
void plantSaplings(QcStructure &structure, const QcShowcaseContext &context, const QcFeatureDefinition &feature, int x)
{
    int offset = 0;
    for (qsizetype type = 0; type < context.content.blocks().size(); ++type) {
        const QcBlockDefinition &block = context.content.blocks()[type];
        const bool giant = block.sapling.giantFeature == feature.name;
        if (block.sapling.feature != feature.name && !giant) {
            continue;
        }

        const int width = giant ? 2 : 1;
        const QcBlockId sapling = context.content.stateTable().firstState(static_cast<quint32>(type));
        for (int dx = 0; dx < width; ++dx) {
            for (int dz = 0; dz < width; ++dz) {
                const QcBlockPos pos{x + offset + dx, 1, treeZ + saplingDistance + dz};
                structure.set(pos.x, 0, pos.z, firstKnown(context.content, block.sapling.plantOn, context.ground));
                structure.set(pos.x, 1, pos.z, sapling);
                structure.addGrowing(pos);
            }
        }
        structure.addLabel({{x + offset + 0.5, 2.5, treeZ + saplingDistance + 0.5}, block.translationKey, {}, false});
        offset += width + 1;
    }
}

// Width of the plot of a tree and where its trunk stands in it.
struct Plot
{
    int width = plotWidth;
    int trunk = plotWidth / 2;
};

Plot plotOf(const QcShowcaseContext &context, const QcFeatureDefinition &feature)
{
    const QcTreeShape::Params params = QcTreeShape::paramsOf(context.content, feature);
    if (!params.giant()) {
        return {};
    }
    const int reach = QcTreeShape::reach(params);
    return {2 * reach + 3, reach};
}

} // namespace

namespace QcShowcaseTreeArea {

QcShowcaseArea build(const QcShowcaseContext &context, QcBlockPos anchor)
{
    QList<std::pair<const QcFeatureDefinition *, Plot>> trees;
    int tallest = 1;
    int width = 0;
    for (const QcFeatureDefinition &feature : context.content.worldgen().features()) {
        if (isTree(feature)) {
            trees.append({&feature, plotOf(context, feature)});
            tallest = std::max(tallest, feature.maxHeight);
            width += trees.last().second.width;
        }
    }

    const int depth = treeZ + saplingDistance + 3;
    auto structure = std::make_shared<QcStructure>(u"showcase/trees"_s, std::max(1, width), tallest + crownRoom + 2, depth);
    int plotStart = 0;
    for (const auto &[feature, plot] : std::as_const(trees)) {
        const int x = plotStart + plot.trunk;
        growTree(*structure, context, *feature, x);
        plantSaplings(*structure, context, *feature, x);
        plotStart += plot.width;
    }

    const QcBlockPos origin{anchor.x, anchor.y, anchor.z - depth + 1};
    return {u"trees"_s, u"showcase.area.trees"_s, std::move(structure), origin, {}, {}};
}

} // namespace QcShowcaseTreeArea
