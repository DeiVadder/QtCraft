#pragma once

#include "QcBlockFace.h"
#include "QcBlockProperty.h"
#include "QcFarmingDefinitions.h"
#include "QcLiquidDefinition.h"
#include "QcMachineDefinition.h"
#include "QcShapeCondition.h"
#include "QcShapeDefinition.h"

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

// What a block needs next to it to stay in place; without it, it breaks and drops.
enum class QcBlockSupport : quint8 {
    None,
    // A full solid top face below (plants, floor torches).
    Below,
    // A full solid face on the side its `attach` property names (torches, ladders).
    Attached,
};

// How a placed block's `facing` follows the player's horizontal view direction.
enum class QcFacingRule : quint8 {
    // Facing where the player looks (stairs climb away, doors open away).
    Away,
    // Facing back at the player (furnace and chest fronts).
    Toward,
};

// What happens to a block when it makes a sound.
enum class QcBlockSoundAction : quint8 {
    Break,
    Place,
    Step,
    // Each strike while mining.
    Hit,
};

inline constexpr int qcBlockSoundActionCount = 4;

struct QcItemDrop
{
    QString item;
    int count = 1;
    // Probability in [0, 1] that this drop appears at all.
    double chance = 1.0;
    // Block drops only: states that drop it (every condition holds); empty for all states. Server side
    // only (crops drop their harvest only when ripe, decision E-289).
    QList<QcShapeCondition> when = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisations warning-free
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

// A plant that grows upwards by itself (sugar cane): random ticks of its top block add a block of the
// same kind above until the column is `maxHeight` tall. Server side only.
struct QcGrowthDefinition
{
    // Tallest column it grows to; 0 for blocks that do not grow.
    int maxHeight = 0;
    // Average seconds between two growth steps of one column.
    double averageSeconds = 600.0;

    bool grows() const { return maxHeight > 0; }
};

struct QcBlockDefinition
{
    QString name;
    QString translationKey;
    // Translation key of the handbook's text about it ("<translation>.description" unless set); a
    // missing text leaves only the facts. Server side only.
    QString descriptionKey;
    QcRenderLayer renderLayer = QcRenderLayer::Opaque;
    // Named state properties; every combination of values is a state with its own runtime id.
    QList<QcBlockProperty> properties;
    // Shape registered under this name; empty uses `shape` (empty again: the full cube).
    QString shapeName;
    QcShapeDefinition shape;
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
    // Sound material ("qtcraft:stone"), empty for silent blocks, and explicit sound names per
    // QcBlockSoundAction that replace the material's sound (see QcSoundKeys).
    QString soundMaterial;
    std::array<QString, qcBlockSoundActionCount> soundKeys;
    // Texture names indexed by QcBlockFace.
    std::array<QString, qcBlockFaceCount> faceTextures;
    // Further named textures that shape parts refer to (door halves), as slot -> texture.
    QList<std::pair<QString, QString>> textureSlots;
    // Item icon drawn flat instead of the block model (torches, doors); empty for the block model.
    QString itemTexture;
    // Block light emitted, 0..15.
    int lightLevel = 0;
    // Entities inside climb up and down (ladders).
    bool climbable = false;
    // Using the block flips its `open` property (doors, trapdoors, gates).
    bool toggles = false;
    // Two blocks tall: placed, opened and broken together; `half` tells bottom from top.
    bool door = false;
    QcFacingRule facingRule = QcFacingRule::Away;
    QcBlockSupport support = QcBlockSupport::None;
    // Connection group this block belongs to and the groups whose members (and full solid blocks)
    // set its north, east, south and west properties (fences, walls, panes).
    QString connectGroup;
    QStringList connectsTo;
    // Item slots of a storage block (chest); 0 for blocks without storage.
    int containerSlots = 0;
    // Tags of the item that places this block (server side only).
    QStringList itemTags;
    // "<material>/<family>" for building parts made by QcPartGenerator, empty otherwise (server side only).
    QString partOf;
    // Flow of a liquid block; distance and movement factor also reach the client.
    QcLiquidDefinition flow;
    // Damage per hit to bodies touching the block (lava); items inside burn. Server side only.
    double contactDamage = 0.0;
    // Factor on the movement of bodies inside the block (cobweb; 1 = no effect). Reaches the client.
    double slowsInside = 1.0;
    // Factors on walking speed and jump of bodies standing on the block (mud). Reach the client.
    double surfaceSpeed = 1.0;
    double surfaceJump = 1.0;
    // Touching a block named `hardensWith` (any state, e.g. water) turns this block into
    // `hardensInto` (concrete powder into concrete). Server side only.
    QString hardensWith;
    QString hardensInto;
    // A liquid flowing into the block breaks it with its drops (plants, torches). Server side only.
    bool washable = false;
    // Without a block below it falls as an entity and lands again (sand, gravel). Server side only.
    bool falls = false;
    // Blocks it may stand on (plants); empty: any full solid top face. Server side only.
    QStringList groundBlocks;
    // A liquid that must touch the ground block from a side (sugar cane: water), unless the ground is
    // this block itself. Server side only.
    QString groundLiquid;
    QcGrowthDefinition growth;
    // Ripening in stages (crops, stems) and being soil for them (farmland); server side only.
    QcCropDefinition crop;
    QcSoilDefinition soil;
    // Block it turns into when an item that `tills` (a hoe) is used on it, empty for none. Server side only.
    QString tillsInto;

    static constexpr int maxLightLevel = 15;
    static constexpr int maxContainerSlots = 54;

    // Whether the server gives the block random ticks (growing plants, crops, soil).
    bool hasRandomTicks() const { return growth.grows() || crop.isCrop() || soil.isSoil(); }
    const QString &texture(QcBlockFace face) const { return faceTextures[std::size_t(face)]; }
    // Texture of a named slot, or empty.
    QString slotTexture(const QString &slot) const
    {
        for (const auto &[name, texture] : textureSlots) {
            if (name == slot) {
                return texture;
            }
        }
        return {};
    }
    const QcBlockProperty *property(const QString &propertyName) const
    {
        for (const QcBlockProperty &candidate : properties) {
            if (candidate.name == propertyName) {
                return &candidate;
            }
        }
        return nullptr;
    }
};
