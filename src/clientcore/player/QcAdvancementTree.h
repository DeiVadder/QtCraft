// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcAdvancementDefinition.h"
#include "common/gameplay/QcAdvancementProgress.h"

#include <QList>

/*! \brief The adventure guide as the tree view shows it (decision E-924), without the GUI: which
    advancements a player sees, how far they are with each, and where each stands, one tree per
    root.

    Visibility follows the role model: an advancement that is not \c {hidden} shows when it is a
    root or its parent shows; a hidden one, and everything below it, stays out of the tree until
    it or one of its descendants is reached; a reached advancement always shows, and with it its
    ancestors, so the lines stay connected. Unreached advancements below a hidden one that has
    not shown yet stay out as well (their parent is not there to hang from). The places come from
    QcTidyTreeLayout over the shown advancements only, in the content's order, so the tree closes
    up around what is hidden. */
namespace QcAdvancementTree {

/*! \brief Reached, some requirement groups met (a progress count such as "2/4" shows it), or
    nothing yet. */
enum class State : quint8 {
    Open,    // nothing yet
    Partial, // some requirement groups met
    Reached  // reached
};

/*! \brief An advancement in the tree: where it stands and how far the player is with it. */
struct Node
{
    /*! Index into the content's advancements (QcAdvancementCatalog::items). */
    qsizetype advancement = 0;
    /*! Index of the parent in the tree's nodes; -1 for the root. */
    qsizetype parent = -1;
    /*! Its column: the depth below the root. */
    int column = 0;
    /*! Its row from the top; halves where a parent is centred on an even number of rows. */
    double row = 0.0;
    /*! How far the player is with it. */
    State state = State::Open;
    /*! Requirement groups with a met criterion, and all groups. */
    int groupsMet = 0;
    /*! All requirement groups. */
    int groups = 0;
};

/*! \brief One tree of the guide with its size. */
struct Tree
{
    /*! The root's index into the content's advancements. */
    qsizetype root = 0;
    /*! The shown advancements of the tree in preorder (the root first). */
    QList<Node> nodes;
    /*! Columns the tree takes. */
    int columns = 0;
    /*! Rows the tree takes. */
    double rows = 0.0;
};

/*! Returns per advancement of \a advancements (the content's, resolved: parents name
    advancements of the list or none) whether the player with \a progress sees it. */
[[nodiscard]] QList<bool> visible(const QList<QcAdvancementDefinition> &advancements, const QcAdvancementProgress &progress);

/*! Returns the trees of the shown roots of \a advancements with the player's \a progress, in the
    content's order. */
[[nodiscard]] QList<Tree> trees(const QList<QcAdvancementDefinition> &advancements, const QcAdvancementProgress &progress);

/*! Returns the parent of each advancement as an index into \a advancements, -1 for a root or a
    parent the list does not have. */
[[nodiscard]] QList<qsizetype> parentIndices(const QList<QcAdvancementDefinition> &advancements);

} // namespace QcAdvancementTree
