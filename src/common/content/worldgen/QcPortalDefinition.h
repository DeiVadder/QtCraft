// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/worldgen/QcDimensionDefinition.h"

#include <QString>
#include <QtGlobal>

/*! \brief A kind of portal between two dimensions (portals/<name>.json of a pack, decision
    E-1020): its frame, its shape, what lights it, the block that fills it, where it leads and
    how travellers wait.

    A portal leads from \c {from} to \c {to}. Lit in \c {from}, it leads to \c {to}; lit in
    \c {to}, it leads back as \c {back} says. In any other dimension the frame does not light.
    The nether portal of the base game is a vertical rectangle of obsidian lit by fire; the ring
    with inserts lit by an item is ready for a dimension like the end. */
struct QcPortalDefinition
{
    /*! The most inner blocks of a frame along a side. */
    static constexpr int maxInner = 64;

    /*! How the frame stands (\c {"shape"}/\c {"type"}). */
    enum class Shape : quint8 {
        /*! A vertical rectangle along x or z (\c {"vertical"}, the default), corners not
            needed. */
        Vertical,
        /*! A horizontal square ring (\c {"ring"}), corners not needed, every frame block with
            its insert. */
        Ring,
    };

    /*! What lights the portal (\c {"trigger"}/\c {"type"}). */
    enum class Trigger : quint8 {
        /*! Fire lit in the empty frame (\c {"fire"}, the default): flint and steel, lava, lightning
            and every later source of fire. */
        Fire,
        /*! An item used on a frame block without its insert puts the insert in (\c {"item"}); the
            last one lights the ring. */
        Item,
    };

    /*! How the destination is found (\c {"link"}/\c {"type"}). */
    enum class Link : quint8 {
        /*! By the coordinate scales of both dimensions (\c {"scaled"}, the default): a portal of
            the same kind near the scaled point, else a new one there. */
        Scaled,
        /*! At the arrival point of the target dimension (\c {"fixed"},
            QcDimensionDefinition::arrival), with its platform. */
        Fixed,
    };

    /*! Where the portal leads when used in \c {to} (\c {"back"}). */
    enum class Back : quint8 {
        /*! Back to \c {from} by the scaled link (\c {"linked"}, the default). */
        Linked,
        /*! To the world spawn (\c {"spawn"}). */
        Spawn,
    };

    /*! Qualified name (\c {"name"}, required). */
    QString name;
    /*! The dimension the portal leads from (\c {"from"}). */
    QString from = QString::fromLatin1(QcDimensionDefinition::overworldName);
    /*! The dimension the portal leads to (\c {"to"}, required). */
    QString to;
    /*! The frame block, any of its states (\c {"frame"} or \c {"frame"}/\c {"block"},
        required). */
    QString frame;
    /*! The property and value a frame block of a ring needs (\c {"frame"}/\c {"insert"}/
        \c {"property"} and \c {"value"}); empty for a frame without inserts. \qcshared */
    QString insertProperty;
    QString insertValue;
    /*! The block that fills a lit frame (\c {"block"}, required); its \c {axis} property, when it
        has one, follows a vertical frame (x or z). */
    QString block;
    /*! How the frame stands. */
    Shape shape = Shape::Vertical;
    /*! The smallest and largest frame from outside to outside (\c {"shape"}/\c {"min"} and
        \c {"max"}: [width, height]; a ring is square, its height the same). \qcshared */
    int minWidth = 4;
    int minHeight = 5;
    int maxWidth = 23;
    int maxHeight = 23;
    /*! What lights the portal. */
    Trigger trigger = Trigger::Fire;
    /*! The item of Trigger::Item (\c {"trigger"}/\c {"item"}). */
    QString triggerItem;
    /*! How the destination is found. */
    Link link = Link::Scaled;
    /*! How far, in overworld blocks, a scaled link looks for a portal of the same kind around the
        scaled point (\c {"link"}/\c {"searchRadius"}); divided by the target's coordinate scale.
        \qcshared */
    int searchRadius = 128;
    /*! How far around the scaled point a new portal looks for ground before it stands where it
        must, on a platform (\c {"link"}/\c {"createRadius"}, in blocks of the target). */
    int createRadius = 16;
    /*! The block of the platform under a portal that stands where it must (\c {"link"}/
        \c {"platform"}); empty: the frame block. */
    QString platform;
    /*! Where the portal leads when used in \c {to}. */
    Back back = Back::Linked;
    /*! Seconds a player in survival or adventure mode stands in the portal before he travels
        (\c {"wait"}/\c {"survival"}), and in creative mode (\c {"wait"}/\c {"creative"}). Other
        travellers go at once. \qcshared */
    double survivalWait = 4.0;
    double creativeWait = 0.0;
    /*! Seconds after a journey before a creature, item or minecart may travel again
        (\c {"cooldown"}); a player has to step out of the portal first. */
    double cooldown = 15.0;
    /*! The sounds (\c {"sounds"}/\c {"ambient"}, \c {"travel"} and \c {"lit"}, qualified sound
        keys); empty: none. \qcshared */
    QString ambientSound;
    QString travelSound;
    QString litSound;

    /*! Returns the inner size from outside to inside: \a outer minus the two frame blocks. */
    static constexpr int innerOf(int outer) { return outer - 2; }
    /*! Returns whether the portal is lit in the dimension \a dimension: its \c {from} or its
        \c {to}. */
    bool lightsIn(const QString &dimension) const { return dimension == from || dimension == to; }
};
