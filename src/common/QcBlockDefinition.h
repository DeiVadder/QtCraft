#pragma once

#include "QcBlockFace.h"
#include "QcMachineDefinition.h"

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

// Geometry the client mesher builds for a block.
enum class QcBlockShape : quint8 {
    Cube,
    // Two crossed, double-sided quads (saplings, flowers); never hides neighbour faces.
    Cross,
};

struct QcItemDrop
{
    QString item;
    int count = 1;
    // Probability in [0, 1] that this drop appears at all.
    double chance = 1.0;
};

// A block that grows into a tree feature after a random time (saplings).
struct QcSaplingDefinition
{
    // Name of a tree or column feature; empty for blocks that are not saplings.
    QString feature;
    double minSeconds = 120.0;
    double maxSeconds = 300.0;
    // Blocks it may be planted on; empty allows any solid block.
    QList<QString> plantOn;
    // Chance that a despawning item of this block plants itself instead of vanishing.
    double plantChance = 0.0;

    bool isSapling() const { return !feature.isEmpty(); }
};

struct QcBlockDefinition
{
    QString name;
    QString translationKey;
    QcRenderLayer renderLayer = QcRenderLayer::Opaque;
    QcBlockShape shape = QcBlockShape::Cube;
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
    // Using the block opens its machine screen (furnace).
    QcMachineDefinition machine;
    // Seconds the block's item burns as fuel in a machine; 0 for blocks that are no fuel.
    double fuelSeconds = 0.0;
    QcSaplingDefinition sapling;
    // Texture names indexed by QcBlockFace.
    std::array<QString, qcBlockFaceCount> faceTextures;

    const QString &texture(QcBlockFace face) const { return faceTextures[std::size_t(face)]; }
};
