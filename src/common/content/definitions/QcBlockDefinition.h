#pragma once

#include "common/content/definitions/QcBlastDefinition.h"
#include "common/content/definitions/QcBlockProperty.h"
#include "common/content/definitions/QcCookingDefinitions.h"
#include "common/content/definitions/QcFarmingDefinitions.h"
#include "common/content/definitions/QcFireDefinition.h"
#include "common/content/definitions/QcLiquidDefinition.h"
#include "common/content/definitions/QcMachineDefinition.h"
#include "common/content/definitions/QcShapeCondition.h"
#include "common/content/definitions/QcShapeDefinition.h"
#include "common/content/definitions/QcSignalDefinition.h"
#include "common/content/definitions/QcSpawnerDefinition.h"
#include "common/core/QcBlockFace.h"
#include "common/world/QcSignText.h"

#include <QList>
#include <QString>

#include <algorithm>
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
    // Facing into the clicked block (a hopper's spout); placed on top, down (decision E-342).
    Into,
    // Towards the clicked block on a side face, where the player looks on the top or bottom (a
    // vertical slab lying against the clicked block, decision E-755).
    Against,
};

// What a rail does to the minecarts on it (decision E-384): a powered rail speeds them up while it
// is powered and brakes them while it is not; an activator rail acts on them while it is powered
// (the rider gets out, a hopper cart stops pulling) and lets a hopper cart pull again while it is not.
enum class QcRailAction : quint8 { None, Boost, Activate };

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
    // Block drops only: more than `count` items, chosen evenly between `count` and this (lapis lazuli
    // drops 4 to 9); 0 for exactly `count`. A later fortune enchantment raises it.
    int maxCount = 0;

    // The most items this drop gives.
    int mostCount() const { return std::max(count, maxCount); }
};

// A sign (decision E-766): `lines` lines of at most `lineLength` characters that players write and
// everyone sees (QcSignTexts on the server, QcMsg::SignText); 0 lines: no sign. Reaches the client.
struct QcSignDefinition
{
    static constexpr int maxLines = QcSignText::maxLines;
    static constexpr int maxLineLength = QcSignText::maxLineLength;

    int lines = 0;
    int lineLength = 15;

    bool isSign() const { return lines > 0; }
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
    // Experience points mining it gives as orbs, with a tool that gets its drops (ores; decision
    // E-398). Server side only.
    QcExperienceDrop experience;
    // An infested block (decision E-453): it stands in for the block `infestsBlock` and lets a
    // creature of kind `infestsMob` out when broken with drops (QcInfestation); both empty for other
    // blocks. Server side only.
    QString infestsBlock;
    QString infestsMob;
    // Using the block opens the 3 x 3 crafting grid instead of placing against it.
    bool opensCrafting = false;
    // Using the block opens its machine screen (furnace).
    QcMachineDefinition machine;
    // Seconds the block's item burns as fuel in a machine; 0 for blocks that are no fuel.
    double fuelSeconds = 0.0;
    QcSaplingDefinition sapling;
    // A monster spawner (decision E-520). Server side only.
    QcSpawnerDefinition spawner;
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
    // Whether the block gets an item of its own name; internal variants (a lit lamp, a piston head)
    // have none and drop the item of their main block. Server side only.
    bool hasItem = true;
    // Block light emitted, 0..15.
    int lightLevel = 0;
    // Entities inside climb up and down (ladders).
    bool climbable = false;
    // Using the block flips its `open` property (doors, trapdoors, gates).
    bool toggles = false;
    // Two blocks tall: placed, opened and broken together; `half` tells bottom from top.
    bool door = false;
    // Two blocks long: placed and broken together, the head in the `facing` direction of the foot;
    // `part` tells foot from head. Villages count them (QcVillages). Server side only (not
    // synchronised).
    bool bed = false;
    // Stairs with corners (decision E-754): the states `facing`, `half` and `shape` (straight,
    // inner_left, inner_right, outer_left, outer_right); the server sets `shape` from the stairs
    // around it (QcStairShapes). Derived from the states, server side only.
    bool stairs = false;
    QcFacingRule facingRule = QcFacingRule::Away;
    QcBlockSupport support = QcBlockSupport::None;
    // Connection group this block belongs to and the groups whose members (and full solid blocks)
    // set its north, east, south and west properties (fences, walls, panes).
    QString connectGroup;
    QStringList connectsTo;
    // Item slots of a storage block (chest); 0 for blocks without storage.
    int containerSlots = 0;
    // Slots per row of its screen (a dispenser's 3 x 3, a hopper's row of 5).
    int containerColumns = 9;
    // Tags of the item that places this block (server side only).
    QStringList itemTags;
    // "<material>/<family>" for building parts made by QcPartGenerator, empty otherwise (server side only).
    QString partOf;
    // Flow of a liquid block; distance and movement factor also reach the client.
    QcLiquidDefinition flow;
    // Damage per hit to bodies touching the block (lava); items inside burn unless `burnsItems` is
    // false (a campfire hurts only creatures and players, decision E-772). Server side only.
    double contactDamage = 0.0;
    bool burnsItems = true;
    // Factor on the movement of bodies inside the block (cobweb; 1 = no effect). Reaches the client.
    double slowsInside = 1.0;
    // Factors on walking speed and jump of bodies standing on the block (mud). Reach the client.
    double surfaceSpeed = 1.0;
    double surfaceJump = 1.0;
    // How slippery the block is to bodies on it, in the units of the original game (decision E-430):
    // the default 0.6 grips, ice 0.98; above 0.6 bodies keep their momentum (QcEntityMotion::step).
    // Reaches the client.
    double slipperiness = grippingSlipperiness;
    // Touching a block named `hardensWith` (any state, e.g. water) turns this block into
    // `hardensInto` (concrete powder into concrete). Server side only.
    QString hardensWith;
    QString hardensInto;
    // Placed on the surface of a liquid (a lily pad, decision E-774): the player aims at the liquid
    // and the block goes into the cell above it. Reaches the client.
    bool floats = false;
    // Eaten bite by bite where it stands (cake, decision E-773); the food reaches the client.
    QcBitesDefinition bites;
    // A campfire (decision E-772); the slots reach the client.
    QcCampfireDefinition campfire;
    // A liquid flowing into the block breaks it with its drops (plants, torches). Server side only.
    bool washable = false;
    // Without a block below it falls as an entity and lands again (sand, gravel). Server side only.
    bool falls = false;
    // Fire, flammability and eternal fire (decision E-392). Server side only.
    QcFireDefinition fire;
    // Blast resistance and explosives (decision E-600). Server side only.
    QcBlastDefinition blast;
    // A rail (decision E-382): minecarts roll along it; its states `facing` and `shape` follow from
    // the rails around it (QcRails). Server side only.
    bool rail = false;
    // What the rail does to carts (`"railAction"`: boost, activate); such rails have a `powered` state.
    QcRailAction railAction = QcRailAction::None;
    // Its top carries blocks that need support below although the top face is not full (rails on a
    // Signal Hopper, decision E-386). Server side only.
    bool supportsAbove = false;
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
    // Block it turns into when an item that `flattens` (a shovel) is used on its top, empty for none
    // (decision E-406). Server side only.
    QString flattensInto;
    // Block it turns into when an item that `carves` (shears) is used on it, facing the clicked side,
    // dropping `carveDropCount` of the item `carveDrop` (a pumpkin carved into a head, decision E-472);
    // empty for none. Server side only.
    QString carvesInto;
    QString carveDrop;
    int carveDropCount = 0;
    // Block it turns into, keeping the state properties both have, when an item that `strips` (an
    // axe) is used on it (a log losing its bark, decision E-753); empty for none. Server side only.
    QString strippedInto;
    // State property a dye sets to its colour when used on the block (QcItemDefinition::dye; the
    // signal indicator light's `color`, decision E-765); empty for none. Server side only.
    QString dyeProperty;
    // Block it turns into at its next block update while an opaque full block lies on it (a dirt path
    // becoming dirt, decision E-406), empty for none. Server side only.
    QString coveredInto;
    // Part in Signal Dust circuits (decision E-333); the kind reaches the client (usable blocks), the
    // rest is server side only.
    QcSignalDefinition signal;
    QcSignDefinition sign;

    static constexpr int maxLightLevel = 15;
    static constexpr int maxContainerSlots = 54;
    // Slipperiness of a block that grips (the default) and the most slippery a block may be.
    static constexpr double grippingSlipperiness = 0.6;
    static constexpr double maxSlipperiness = 0.995;

    // Whether creatures keep from spawning on or in the block because it hurts or slows them (lava,
    // cobweb; QcNaturalSpawner).
    bool keepsSpawnsAway() const { return contactDamage > 0.0 || slowsInside < 1.0; }
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
