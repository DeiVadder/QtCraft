#pragma once

#include "QcAabb.h"
#include "QcBlockFace.h"

#include <QList>
#include <QString>

#include <array>

// Everything that depends on one concrete block state, derived from the block definition and its
// shape. Boxes are block-local (0..1, collision up to 1.5 m high).
struct QcBlockState
{
    // Index of the block definition and of the state among that block's states.
    quint32 type = 0;
    quint32 index = 0;
    QList<QcAabb> boxes;
    // Texture slot per box (parallel to `boxes`); empty means the face textures.
    QList<QString> boxTextures;
    // Whether a box's faces stretch the whole texture over themselves (parallel to `boxes`).
    QList<bool> boxStretch;
    QList<QcAabb> crosses;
    QList<QcAabb> collision;
    // The shape is exactly the unit cube.
    bool fullCube = true;
    // Full cube of an opaque block: hides neighbour faces completely.
    bool opaque = true;
    // Light lost entering the cell; 0 for partial shapes, which block light at covered faces.
    int lightOpacity = 15;
    // Faces light cannot cross (bits per QcBlockFace, see QcLightPassage).
    quint8 lightBlockedFaces = 0;
    // Whether the boxes cover the whole side of the cell, per QcBlockFace (supports torches).
    std::array<bool, qcBlockFaceCount> fullFace{};
    // Face of the definition whose texture shows on each world face (turned blocks such as furnaces).
    std::array<QcBlockFace, qcBlockFaceCount> faceSource = qcAllBlockFaces;

    // Boxes that can be pointed at: the drawn boxes and the regions of crossed planes.
    QList<QcAabb> targetBoxes() const { return boxes + crosses; }
};
