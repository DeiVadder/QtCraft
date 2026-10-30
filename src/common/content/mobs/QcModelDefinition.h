// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Skeleton models for creatures and players: plain data structs grouped here because they only
// make sense together. Coordinates are metres relative to the entity's feet centre, facing north
// (-Z); the entity's right side is +X.

#include "common/core/QcVec3.h"

#include <QList>
#include <QString>

/*! \brief What the animator does with a bone.

    Bones without a role only follow the whole body. */
enum class QcBoneRole : quint8 {
    None, // follows the whole body only
    Head, // turns and tilts with the entity's look
    Body, // the torso
    RightArm, // the right arm
    LeftArm, // the left arm
    RightLeg, // the right leg
    LeftLeg, // the left leg
};

/*! \brief One box of a bone.

    \c {slot} names the part (e.g. "head", "torso") that an outfit textures. */
struct QcModelBox
{
    /*! Lower corner (\c {"from"}), below \c {to} on every axis. */
    QcVec3 from;
    /*! Upper corner (\c {"to"}). */
    QcVec3 to;
    /*! Name of the part an outfit textures (\c {"slot"}). */
    QString slot;
};

/*! \brief One bone of a skeleton model: boxes that turn together around a pivot. */
struct QcModelBone
{
    /*! Name of the bone (\c {"name"}, at most 64 characters). */
    QString name;
    /*! What the animator does with it (\c {"role"}). */
    QcBoneRole role = QcBoneRole::None;
    /*! Point the bone turns around (shoulder, hip, neck). */
    QcVec3 pivot;
    /*! Its boxes (\c {"boxes"}), 1 to QcModelDefinition::maxBoxesPerBone. */
    QList<QcModelBox> boxes;
};

/*! \brief A skeleton model for creatures and players (a file in \c {models}): named bones with
    boxes that outfits texture by slot. */
struct QcModelDefinition
{
    /*! Most bones of a model. */
    static constexpr int maxBones = 32;
    /*! Most boxes of a bone. */
    static constexpr int maxBoxesPerBone = 64;

    /*! Content name of the model (\c {"name"}). */
    QString name;
    /*! Its bones (\c {"bones"}), 1 to maxBones. */
    QList<QcModelBone> bones;
};
