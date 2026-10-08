#pragma once

#include "QcBlockFace.h"

#include <QList>
#include <QString>

#include <array>

// Runtime block id: index into the registry, assigned at registration. Never persisted;
// persistent data refers to blocks by name.
using QcBlockId = quint32;

inline constexpr QcBlockId qcAirBlockId = 0;

enum class QcRenderLayer : quint8 {
    Invisible,
    Opaque,
    // Alpha-tested (e.g. leaves): drawn in the opaque pass, does not hide neighbour faces.
    Cutout,
    // Alpha-blended (e.g. water, glass).
    Translucent,
};

struct QcItemDrop
{
    QString item;
    int count = 1;
};

struct QcBlockDefinition
{
    QString name;
    QString translationKey;
    QcRenderLayer renderLayer = QcRenderLayer::Opaque;
    // Solid blocks collide with entities.
    bool solid = true;
    // Opaque blocks are full cubes that hide adjacent faces and block sky light.
    bool opaque = true;
    bool liquid = false;
    // Replaceable blocks (air, water, tall grass) are overwritten when a block is placed into them.
    bool replaceable = false;
    // Sky light reduction per block in [0, 15]; 15 blocks light completely.
    int lightOpacity = 15;
    // Seconds to break by hand; negative means unbreakable.
    double hardness = 1.0;
    // Tool category that mines this block faster and may be required for drops, empty for none.
    QString tool;
    // Minimum tool tier for drops; 0 means hand works.
    int toolLevel = 0;
    QList<QcItemDrop> drops;
    // Using the block opens the 3 x 3 crafting grid instead of placing against it.
    bool opensCrafting = false;
    // Texture names indexed by QcBlockFace.
    std::array<QString, qcBlockFaceCount> faceTextures;

    const QString &texture(QcBlockFace face) const { return faceTextures[std::size_t(face)]; }
};
