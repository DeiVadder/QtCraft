#include "clientcore/player/QcAdvancementTree.h"

#include "clientcore/player/QcTidyTreeLayout.h"

#include <QHash>

#include <algorithm>

namespace {

// The parents in preorder before their children: the order in which visibility is decided top down.
QList<qsizetype> topDown(const QList<qsizetype> &parents)
{
    QList<QList<qsizetype>> children(parents.size());
    QList<qsizetype> roots;
    for (qsizetype index = 0; index < parents.size(); ++index) {
        if (parents[index] < 0) {
            roots.append(index);
        } else {
            children[parents[index]].append(index);
        }
    }

    QList<qsizetype> order;
    QList<qsizetype> stack(roots.crbegin(), roots.crend());
    while (!stack.isEmpty()) {
        const qsizetype index = stack.takeLast();
        order.append(index);
        stack.append(QList<qsizetype>(children[index].crbegin(), children[index].crend()));
    }
    return order;
}

QcAdvancementTree::Node nodeOf(const QcAdvancementDefinition &advancement, const QcAdvancementProgress &progress)
{
    const QSet<QString> met = progress.criteria(advancement.name);
    QcAdvancementTree::Node node;
    node.groups = static_cast<int>(advancement.requirements.size());
    node.groupsMet = static_cast<int>(std::ranges::count_if(advancement.requirements, [&met](const QStringList &group) {
        return std::ranges::any_of(group, [&met](const QString &criterion) { return met.contains(criterion); });
    }));
    if (advancement.isDone(met)) {
        node.state = QcAdvancementTree::State::Reached;
    } else if (node.groupsMet > 0) {
        node.state = QcAdvancementTree::State::Partial;
    }
    return node;
}

} // namespace

namespace QcAdvancementTree {

QList<qsizetype> parentIndices(const QList<QcAdvancementDefinition> &advancements)
{
    QHash<QString, qsizetype> indexOf;
    for (qsizetype index = 0; index < advancements.size(); ++index) {
        indexOf.insert(advancements[index].name, index);
    }
    QList<qsizetype> parents;
    parents.reserve(advancements.size());
    for (const QcAdvancementDefinition &advancement : advancements) {
        parents.append(advancement.parent.isEmpty() ? -1 : indexOf.value(advancement.parent, -1));
    }
    return parents;
}

QList<bool> visible(const QList<QcAdvancementDefinition> &advancements, const QcAdvancementProgress &progress)
{
    const QList<qsizetype> parents = parentIndices(advancements);
    // Reached ones and their ancestors show whatever else holds.
    QList<bool> shown(advancements.size(), false);
    for (qsizetype index = 0; index < advancements.size(); ++index) {
        if (!progress.isDone(advancements[index])) {
            continue;
        }
        for (qsizetype at = index; at >= 0 && !shown[at]; at = parents[at]) {
            shown[at] = true;
        }
    }
    const QList<qsizetype> order = topDown(parents);
    for (const qsizetype index : order) {
        const qsizetype parent = parents[index];
        shown[index] = shown[index] || (!advancements[index].hidden && (parent < 0 || shown[parent]));
    }
    return shown;
}

QList<Tree> trees(const QList<QcAdvancementDefinition> &advancements, const QcAdvancementProgress &progress)
{
    const QList<bool> shown = visible(advancements, progress);
    // Hidden ones leave the layout: their parent becomes -2, which roots nothing.
    QList<qsizetype> parents = parentIndices(advancements);
    for (qsizetype index = 0; index < parents.size(); ++index) {
        if (!shown[index]) {
            parents[index] = -2;
        }
    }

    QList<Tree> result;
    for (qsizetype root = 0; root < advancements.size(); ++root) {
        if (parents[root] != -1) {
            continue;
        }
        const QcTidyTreeLayout::Layout layout = QcTidyTreeLayout::layout(parents, root);
        Tree tree{root, {}, layout.columns, layout.rows};
        QHash<qsizetype, qsizetype> nodeIndex;
        for (const QcTidyTreeLayout::Place &place : layout.places) {
            Node node = nodeOf(advancements[place.node], progress);
            node.advancement = place.node;
            node.parent = place.node == root ? -1 : nodeIndex.value(parents[place.node], -1);
            node.column = place.column;
            node.row = place.row;
            nodeIndex.insert(place.node, tree.nodes.size());
            tree.nodes.append(node);
        }
        result.append(tree);
    }
    return result;
}

} // namespace QcAdvancementTree
