#pragma once

// Skeleton models for creatures and players: plain data structs grouped here because they only
// make sense together. Coordinates are metres relative to the entity's feet centre, facing north
// (-Z); the entity's right side is +X.

#include "QcVec3.h"

#include <QList>
#include <QString>

// What the animator does with a bone. Bones without a role only follow the whole body.
enum class QcBoneRole : quint8 {
    None,
    Head,
    Body,
    RightArm,
    LeftArm,
    RightLeg,
    LeftLeg,
};

// One box of a bone. `slot` names the part (e.g. "head", "torso") that an outfit textures.
struct QcModelBox
{
    QcVec3 from;
    QcVec3 to;
    QString slot;
};

struct QcModelBone
{
    QString name;
    QcBoneRole role = QcBoneRole::None;
    // Point the bone turns around (shoulder, hip, neck).
    QcVec3 pivot;
    QList<QcModelBox> boxes;
};

struct QcModelDefinition
{
    static constexpr int maxBones = 32;
    static constexpr int maxBoxesPerBone = 64;

    QString name;
    QList<QcModelBone> bones;
};
