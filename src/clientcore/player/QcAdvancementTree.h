#pragma once

#include "common/content/definitions/QcAdvancementDefinition.h"
#include "common/gameplay/QcAdvancementProgress.h"

#include <QList>

// The adventure guide as the tree view shows it (decision E-924), without the GUI: which
// advancements a player sees, how far he is with each, and where each stands, one tree per root.
//
// Visibility follows the role model: an advancement that is not `hidden` shows when it is a root or
// its parent shows; a hidden one, and everything below it, stays out of the tree until it or one of
// its descendants is reached; a reached advancement always shows, and with it its ancestors, so the
// lines stay connected. Unreached advancements below a hidden one that has not shown yet stay out as
// well (their parent is not there to hang from). The places come from QcTidyTreeLayout over the
// shown advancements only, in the content's order, so the tree closes up around what is hidden.
namespace QcAdvancementTree {

// Reached, some requirement groups met (a progress count such as "2/4" shows it), or nothing yet.
enum class State : quint8 { Open, Partial, Reached };

struct Node
{
    // Index into the content's advancements (QcAdvancementCatalog::items).
    qsizetype advancement = 0;
    // Index of the parent in the tree's nodes; -1 for the root.
    qsizetype parent = -1;
    int column = 0;
    double row = 0.0;
    State state = State::Open;
    // Requirement groups with a met criterion, and all groups.
    int groupsMet = 0;
    int groups = 0;
};

struct Tree
{
    // The root's index into the content's advancements.
    qsizetype root = 0;
    // The shown advancements of the tree in preorder (the root first).
    QList<Node> nodes;
    int columns = 0;
    double rows = 0.0;
};

// Per advancement of `advancements` (the content's, resolved: parents name advancements of the
// list or none) whether the player with `progress` sees it.
[[nodiscard]] QList<bool> visible(const QList<QcAdvancementDefinition> &advancements, const QcAdvancementProgress &progress);

// The trees of the shown roots, in the content's order.
[[nodiscard]] QList<Tree> trees(const QList<QcAdvancementDefinition> &advancements, const QcAdvancementProgress &progress);

// The parent of each advancement as an index into `advancements`, -1 for a root or a parent the
// list does not have.
[[nodiscard]] QList<qsizetype> parentIndices(const QList<QcAdvancementDefinition> &advancements);

} // namespace QcAdvancementTree
