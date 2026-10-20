#pragma once

#include "common/core/QcAabb.h"
#include "common/core/QcBlockFace.h"

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
    // Texture slot per cross (parallel to `crosses`); empty means the north face texture.
    QList<QString> crossTextures;
    QList<QcAabb> collision;
    // Boxes pointed at instead of the drawn ones (QcShapePart::target); empty when no part has any.
    QList<QcAabb> targets;
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
    // Quarter turns (0..3, clockwise as the face is seen) of the texture on each world face of a full
    // cube: the top of a turned block (glazed terracotta) and the bark of a lying log run with the
    // block (decision E-751).
    std::array<quint8, qcBlockFaceCount> faceUvTurns{};
    // Sixteenths of a turn (0..3, clockwise seen from above) the drawn faces turn beyond the quarter
    // turns of the boxes: the 16 directions of a standing sign (QcShapeDefinition::spin).
    quint8 fineTurns = 0;

    // Boxes that can be pointed at: the drawn boxes and the regions of crossed planes, or the shape's
    // own target boxes.
    QList<QcAabb> targetBoxes() const { return targets.isEmpty() ? boxes + crosses : targets; }
};
