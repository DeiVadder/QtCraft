// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

/*! \brief A soul anchor (decision E-1112, JSON \c {"anchor"} of a block): one block per charge,
    chained to the blocks of the next and the previous charge.

    Used with its \c {fuel} it becomes \c {charged}; used otherwise while charged it sets the
    respawn point in a dimension where anchors work (QcDimensionDefinition::anchorsWork) and
    explodes anywhere else; a respawn there takes one charge and leaves \c {spent}. Server side
    only: the client learns nothing of it and the server corrects what it predicted. */
struct QcAnchorDefinition
{
    /*! The most charges an anchor holds (the reference's four). */
    static constexpr int maxCharge = 4;

    /*! Charges it holds, 0 to maxCharge (\c {"charge"}); -1 for a block that is no anchor. */
    int charge = -1;
    /*! The block with one charge more (\c {"charged"}); empty when it is full. */
    QString charged;
    /*! The block with one charge less (\c {"spent"}); empty when it is empty. */
    QString spent;
    /*! The item that charges it (\c {"fuel"}). */
    QString fuel;

    /*! Returns whether the block is a soul anchor. */
    bool isAnchor() const { return charge >= 0; }
};
