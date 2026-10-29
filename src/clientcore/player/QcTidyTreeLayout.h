#pragma once

#include <QList>
#include <QtGlobal>

// A tidy layout of a tree read from left to right (decision E-924; the advancement tree): a node's
// column is its depth, children follow their parent in one column further right in the order of
// their indices, and every parent stands halfway between its first and its last child. Each
// subtree is pushed up against the ones before it as far as their outlines allow (the contours of
// Reingold and Tilford, without spreading the subtrees between), so two nodes in one column are
// always at least one row apart and a deep branch can sit beside the leaves of a shallow one.
// The result depends on the tree alone: the same parents give the same places.
namespace QcTidyTreeLayout {

struct Place
{
    // The node's index in the parents given to layout().
    qsizetype node = 0;
    int column = 0;
    // Rows from the top; halves where a parent is centred on an even number of rows.
    double row = 0.0;
};

struct Layout
{
    // The nodes of the tree in preorder (the root first).
    QList<Place> places;
    // How many columns and rows the tree takes (the highest row plus one).
    int columns = 0;
    double rows = 0.0;
};

// Places the tree below `root`; `parents[i]` is the index of node i's parent, -1 for a root.
// Indices outside the list and loops are not followed (each node is placed at most once).
[[nodiscard]] Layout layout(const QList<qsizetype> &parents, qsizetype root);

} // namespace QcTidyTreeLayout
