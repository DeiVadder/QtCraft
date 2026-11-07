// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/worldgen/QcNetherDefinition.h"
#include "common/core/QcBlockBox.h"
#include "common/core/QcBlockPos.h"
#include "common/core/QcXzRect.h"
#include "worldgen/fortress/QcFortressPlan.h"

#include <QList>
#include <QString>

/*! \brief One bastion ruin as QcBastionBuilder writes it (decision E-1087): a list of strokes,
    each a box of cells painted one way, in order, from the seed and the terrain noise alone, so
    every chunk writes its own cells without looking at its neighbours. Cells are world positions.

    A floor is the cell of the floor block; one stands one cell above it. */
struct QcBastionPlan
{
    /*! A side, as in a fortress. */
    using Side = QcFortressPlan::Side;

    /*! The most columns from the middle of a bastion to anything of it, its landings included. */
    static constexpr int reach = QcNetherSettings::Bastion::reach;
    /*! The cells between a bastion's reach and the edge of its region. */
    static constexpr int regionMargin = QcNetherSettings::Bastion::regionMargin;

    /*! \brief The forms a bastion takes. */
    enum class Form : quint8
    {
        /*! Two towers joined by a causeway over a canal of lava. */
        Bridge,
        /*! A walled yard with stalls of beasts under a loft. */
        Stables,
        /*! A great hall with a lava floor and a pillar of gold in its middle. */
        Treasure,
        /*! A walled quarter of houses joined by bridges. */
        Housing,
    };
    /*! The number of forms. */
    static constexpr int formCount = 4;

    /*! \brief How a stroke paints its cells. */
    enum class Paint : quint8
    {
        /*! Emptied. */
        Air,
        /*! Plain rough stone. */
        Rough,
        /*! Bricks, some of them cracked and a few gilded. */
        Brick,
        /*! Polished stone. */
        Polished,
        /*! Chiseled stone. */
        Chiseled,
        /*! Stone with gold in it. */
        Gilded,
        /*! Blocks of gold. */
        Gold,
        /*! The soil of a stall. */
        Soil,
        /*! Standing lava, in a basin that holds it. */
        Lava,
        /*! A wall running along the axis of the stroke's side. */
        Wall,
        /*! Stairs climbing towards the stroke's side. */
        Stairs,
        /*! A lantern standing on what lies below. */
        Lantern,
        /*! Each column of the box down to the first solid cell, through lava too. */
        Foundation,
        /*! Solid cells fall away, more of them the higher they lie in the box. */
        Crumble,
    };

    /*! \brief The tables that fill a bastion's chests. */
    enum class Loot : quint8
    {
        /*! The bridge. */
        Bridge,
        /*! The stables. */
        Stables,
        /*! The treasure on the pillar. */
        Treasure,
        /*! Everywhere else. */
        Other,
    };

    /*! \brief Those who stand in a bastion from the start (decision E-1089). */
    enum class Folk : quint8
    {
        /*! The common guard. */
        Guard,
        /*! The bruiser, who never comes back. */
        Bruiser,
        /*! A beast of the stables. */
        Beast,
    };

    /*! \brief A box of cells painted one way. \qcfields */
    struct Stroke
    {
        QcBlockBox box;
        Paint paint = Paint::Air;
        Side side = Side::North;
    };

    /*! \brief A chest at \c {at}, its front towards \c {facing}, filled from \c {loot}.
        \qcfields */
    struct Chest
    {
        QcBlockPos at;
        Side facing = Side::North;
        Loot loot = Loot::Other;
    };

    /*! \brief One of \c {folk} with its feet in the cell \c {at}. \qcfields */
    struct Resident
    {
        QcBlockPos at;
        Folk folk = Folk::Guard;
    };

    /*! \brief A floating sign at \c {at} with the text key \c {text}, \c {heading} for the name of
        the whole. \qcfields */
    struct Sign
    {
        QcBlockPos at;
        QString text;
        bool heading = false;
    };

    /*! The form. */
    Form form = Form::Bridge;
    /*! The region of the plan. \qcshared */
    int regionX = 0;
    int regionZ = 0;
    /*! The middle of the bastion on its floor. */
    QcBlockPos centre;
    /*! The feet cell before its way in, on the landing. */
    QcBlockPos entrance;
    /*! What is painted, in order. */
    QList<Stroke> strokes;
    /*! The chests. */
    QList<Chest> chests;
    /*! The spawners. */
    QList<QcBlockPos> spawners;
    /*! Those who stand in it from the start. */
    QList<Resident> residents;
    /*! The signs a demonstration world shows. */
    QList<Sign> signs;
    /*! The boxes of its halls, yards and decks, as a spawn area takes them (decision E-1074). */
    QList<QcBlockBox> parts;
    /*! The columns the bastion and its seals may touch. */
    QcXzRect bounds;

    /*! Returns whether a cell of the plan may lie in \a area. */
    [[nodiscard]] bool touches(QcXzRect area) const { return bounds.intersects(area); }
    /*! Returns its middle: where it stands, for /locate. */
    [[nodiscard]] QcBlockPos middle() const { return centre.offset(0, 1, 0); }
};
