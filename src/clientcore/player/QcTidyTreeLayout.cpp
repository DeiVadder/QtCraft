// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/player/QcTidyTreeLayout.h"

#include <algorithm>
#include <ranges>
#include <utility>

namespace {

// The outline of a subtree per depth below its root (index 0: the root itself), as rows relative
// to the root's row.
struct Contour
{
    QList<double> top;
    QList<double> bottom;

    // Adds `other` moved down by `shift` rows.
    void merge(const Contour &other, double shift)
    {
        for (qsizetype depth = 0; depth < other.top.size(); ++depth) {
            if (depth < top.size()) {
                top[depth] = std::min(top[depth], other.top[depth] + shift);
                bottom[depth] = std::max(bottom[depth], other.bottom[depth] + shift);
            } else {
                top.append(other.top[depth] + shift);
                bottom.append(other.bottom[depth] + shift);
            }
        }
    }

    // How far `other` must move down to keep one row from this outline at every depth both have.
    [[nodiscard]] double shiftBelow(const Contour &other) const
    {
        double shift = 0.0;
        const qsizetype shared = std::min(top.size(), other.top.size());
        for (qsizetype depth = 0; depth < shared; ++depth) {
            shift = std::max(shift, bottom[depth] - other.top[depth] + 1.0);
        }
        return shift;
    }
};

// The children of every node in index order.
QList<QList<qsizetype>> childrenOf(const QList<qsizetype> &parents)
{
    QList<QList<qsizetype>> children(parents.size());
    for (qsizetype node = 0; node < parents.size(); ++node) {
        const qsizetype parent = parents[node];
        if (parent >= 0 && parent < parents.size() && parent != node) {
            children[parent].append(node);
        }
    }
    return children;
}

// The tree below `root` in preorder with each node's depth, every node once.
QList<std::pair<qsizetype, int>> preorder(const QList<QList<qsizetype>> &children, qsizetype root)
{
    QList<std::pair<qsizetype, int>> order;
    QList<bool> seen(children.size(), false);
    QList<std::pair<qsizetype, int>> stack{{root, 0}};
    while (!stack.isEmpty()) {
        const auto [node, depth] = stack.takeLast();
        if (seen[node]) {
            continue;
        }
        seen[node] = true;
        order.append({node, depth});
        for (qsizetype child = children[node].size() - 1; child >= 0; --child) {
            stack.append({children[node][child], depth + 1});
        }
    }
    return order;
}

} // namespace

namespace QcTidyTreeLayout {

Layout layout(const QList<qsizetype> &parents, qsizetype root)
{
    if (root < 0 || root >= parents.size()) {
        return {};
    }

    const QList<QList<qsizetype>> children = childrenOf(parents);
    const QList<std::pair<qsizetype, int>> order = preorder(children, root);
    QList<bool> placed(parents.size(), false);
    for (const auto &[node, depth] : order) {
        placed[node] = true;
    }

    // Bottom up: each node's outline and its children's rows relative to it.
    QList<Contour> contours(parents.size());
    QList<double> offset(parents.size(), 0.0);
    for (const auto &entry : std::ranges::reverse_view(order)) {
        const qsizetype node = entry.first;
        QList<qsizetype> own;
        for (const qsizetype child : children[node]) {
            if (placed[child] && !contours[child].top.isEmpty()) {
                own.append(child);
            }
        }
        Contour below;
        for (const qsizetype child : own) {
            offset[child] = below.top.isEmpty() ? 0.0 : below.shiftBelow(contours[child]);
            below.merge(contours[child], offset[child]);
            contours[child] = {};
        }
        const double centre = own.isEmpty() ? 0.0 : (offset[own.constFirst()] + offset[own.constLast()]) / 2.0;
        for (const qsizetype child : own) {
            offset[child] -= centre;
        }
        Contour contour{{0.0}, {0.0}};
        for (qsizetype depth = 0; depth < below.top.size(); ++depth) {
            contour.top.append(below.top[depth] - centre);
            contour.bottom.append(below.bottom[depth] - centre);
        }
        contours[node] = std::move(contour);
    }

    // Top down: absolute rows, then moved so the topmost row is 0.
    QList<double> row(parents.size(), 0.0);
    Layout result;
    double top = 0.0;
    double bottom = 0.0;
    for (const auto &[node, depth] : order) {
        const qsizetype parent = parents[node];
        row[node] = node == root ? 0.0 : row[parent] + offset[node];
        top = std::min(top, row[node]);
        bottom = std::max(bottom, row[node]);
        result.places.append({node, depth, row[node]});
        result.columns = std::max(result.columns, depth + 1);
    }
    for (Place &place : result.places) {
        place.row -= top;
    }
    result.rows = bottom - top + 1.0;
    return result;
}

} // namespace QcTidyTreeLayout
