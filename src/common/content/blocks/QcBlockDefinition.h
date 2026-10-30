// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blockbehaviour/QcBlastDefinition.h"
#include "common/content/blockbehaviour/QcFireDefinition.h"
#include "common/content/blockbehaviour/QcLiquidDefinition.h"
#include "common/content/blockbehaviour/QcSignalDefinition.h"
#include "common/content/blockbehaviour/QcSpawnerDefinition.h"
#include "common/content/blocks/QcBlockProperty.h"
#include "common/content/blocks/QcShapeCondition.h"
#include "common/content/blocks/QcShapeDefinition.h"
#include "common/content/items/QcAnvilSwap.h"
#include "common/content/items/QcHoldDisplay.h"
#include "common/content/recipes/QcCookingDefinitions.h"
#include "common/content/recipes/QcFarmingDefinitions.h"
#include "common/content/recipes/QcMachineDefinition.h"
#include "common/core/QcBlockFace.h"
#include "common/world/QcSignText.h"

#include <QList>
#include <QString>

#include <algorithm>
#include <array>

/*! Runtime block id: index into the registry, assigned at registration. Never persisted;
    persistent data refers to blocks by name. */
using QcBlockId = quint32;

/*! Id of air, the one block the registry defines itself and the empty value of chunk storage. */
inline constexpr QcBlockId qcAirBlockId = 0;

/*! \brief Render pass of a block (\c {"render"}: opaque, cutout, translucent or invisible).

    Cross-shaped blocks default to cutout, all others to opaque. */
enum class QcRenderLayer : quint8 {
    Invisible, // Not drawn at all (air).
    Opaque, // Drawn in the opaque pass without transparency.
    /*! Alpha-tested (e.g. leaves): drawn in the opaque pass, does not hide neighbour faces. */
    Cutout,
    /*! Alpha-blended (e.g. water, glass). */
    Translucent,
};

/*! \brief What a block needs next to it to stay in place; without it, it breaks and drops. */
enum class QcBlockSupport : quint8 {
    None, // Needs nothing next to it.
    /*! A full solid top face below (plants, floor torches). */
    Below,
    /*! A full solid face on the side its \c {attach} property names (torches, ladders). */
    Attached,
};

/*! \brief How a placed block's \c {facing} follows the player's horizontal view direction. */
enum class QcFacingRule : quint8 {
    /*! Facing where the player looks (stairs climb away, doors open away). */
    Away,
    /*! Facing back at the player (furnace and chest fronts). */
    Toward,
    /*! Facing into the clicked block (a hopper's spout); placed on top, down (decision E-342). */
    Into,
    /*! Towards the clicked block on a side face, where the player looks on the top or bottom (a
        vertical slab lying against the clicked block, decision E-755). */
    Against,
};

/*! \brief What a rail does to the minecarts on it (decision E-384): a powered rail speeds them
    up while it is powered and brakes them while it is not; an activator rail acts on them while
    it is powered (the rider gets out, a hopper cart stops pulling) and lets a hopper cart pull
    again while it is not. */
enum class QcRailAction : quint8 {
    None, // A plain rail (\c {"railAction"} none or missing).
    Boost, // A powered rail (\c {"railAction": "boost"}).
    Activate // An activator rail (\c {"railAction": "activate"}).
};

/*! \brief What happens to a block when it makes a sound. */
enum class QcBlockSoundAction : quint8 {
    Break, // The block is broken.
    Place, // The block is placed.
    Step, // A body walks on the block.
    /*! Each strike while mining. */
    Hit,
};

/*! Number of values of QcBlockSoundAction. */
inline constexpr int qcBlockSoundActionCount = 4;

/*! \brief An item that a block (or other content) drops, with count, chance and conditions. */
struct QcItemDrop
{
    /*! Qualified name of the dropped item (\c {"item"}). */
    QString item;
    /*! Number of items dropped (\c {"count"}, 1 to 64 for blocks). */
    int count = 1;
    /*! Probability in [0, 1] that this drop appears at all. */
    double chance = 1.0;
    /*! Block drops only: states that drop it (every condition holds); empty for all states.
        Server side only (crops drop their harvest only when ripe, decision E-289). */
    QList<QcShapeCondition> when = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisations warning-free
    /*! Block drops only: more than \c {count} items, chosen evenly between \c {count} and this
        (lapis lazuli drops 4 to 9); 0 for exactly \c {count}. A later fortune enchantment raises
        it. */
    int maxCount = 0;

    /*! Returns the most items this drop gives. */
    int mostCount() const { return std::max(count, maxCount); }
};

/*! \brief A sign (decision E-766): \c {lines} lines of at most \c {lineLength} characters that
    players write and everyone sees (QcSignTexts on the server, QcMsg::SignText); 0 lines: no sign.

    Reaches the client. */
struct QcSignDefinition
{
    /*! Most lines a sign may have (QcSignText::maxLines). */
    static constexpr int maxLines = QcSignText::maxLines;
    /*! Most characters a sign line may have (QcSignText::maxLineLength). */
    static constexpr int maxLineLength = QcSignText::maxLineLength;

    /*! Number of lines (\c {"sign"} object, \c {"lines"}; \c {maxLines} when the object omits it);
        0 for blocks that are no sign. */
    int lines = 0;
    /*! Characters per line (\c {"lineLength"}, 1 to \c {maxLineLength}). */
    int lineLength = 15;

    /*! Returns whether the block is a sign (it has at least one line). */
    bool isSign() const { return lines > 0; }
};

/*! \brief Definition of a block type as read from a content pack's JSON (QcBlockParser).

    The parts that reach the client go through QcBlockSerializer; fields marked server side only
    stay on the server. */
struct QcBlockDefinition
{
    /*! Qualified block name (\c {"name"}, e.g. "qtcraft:stone"). */
    QString name;
    /*! Translation key of its display name (\c {"translation"}; derived from the name unless set,
        "block.<namespace>.<path>"). */
    QString translationKey;
    /*! Translation key of the handbook's text about it ("<translation>.description" unless set);
        a missing text leaves only the facts. Server side only. */
    QString descriptionKey;
    /*! Render pass of the block (\c {"render"}). */
    QcRenderLayer renderLayer = QcRenderLayer::Opaque;
    /*! Named state properties; every combination of values is a state with its own runtime id. */
    QList<QcBlockProperty> properties;
    /*! Shape registered under this name (\c {"shape"} as a string; "cube" and "cross" name the
        base shapes, "cube" leaves it empty); empty uses \c {shape} (empty again: the full cube). */
    QString shapeName;
    /*! Inline shape (\c {"shape"} as an object); after the content sync, the resolved shape. */
    QcShapeDefinition shape;
    /*! Solid blocks collide with entities. */
    bool solid = true;
    /*! Opaque blocks are full cubes that hide adjacent faces and block sky light. */
    bool opaque = true;
    /*! Liquid block (\c {"liquid"}); its flow is in \c {flow}. */
    bool liquid = false;
    /*! Replaceable blocks (air, water, tall grass) are overwritten when a block is placed into
        them. */
    bool replaceable = false;
    /*! Sky light reduction per block in [0, 15]; 15 blocks light completely. */
    int lightOpacity = 15;
    /*! Seconds to break by hand; negative means unbreakable. */
    double hardness = 1.0;
    /*! Tool category that mines this block faster and may be required for drops, empty for none. */
    QString tool;
    /*! Minimum tool tier for drops; 0 means hand works. */
    int toolLevel = 0;
    /*! Items dropped when broken (\c {"drops"}, at most 64 entries). */
    QList<QcItemDrop> drops;
    /*! Experience points mining it gives as orbs, with a tool that gets its drops (ores;
        decision E-398). Server side only. */
    QcExperienceDrop experience;
    /*! An infested block (decision E-453): it stands in for the block \c {infestsBlock} and lets
        a creature of kind \c {infestsMob} out when broken with drops (QcInfestation); both empty
        for other blocks. Server side only. \qcshared */
    QString infestsBlock;
    QString infestsMob;
    /*! Using the block opens the 3 x 3 crafting grid instead of placing against it. */
    bool opensCrafting = false;
    /*! Using the block opens the stonecutter's screen (decision E-785). */
    bool opensStonecutter = false;
    /*! Using the block opens the enchanting table's screen (decision E-864). */
    bool opensEnchanting = false;
    /*! Using the block opens the anvil's screen (decision E-866). */
    bool opensAnvil = false;
    /*! The anvil this one wears into when used or after a fall (chipped, damaged); empty: it
        breaks. Server side only. */
    QString wearsInto;
    /*! The block the anvil turns this one into and back (decision E-870; QcAnvilSwaps puts it on
        both items), declared on one of the two blocks. Server side only. */
    std::optional<QcAnvilSwap> anvilSwap;
    /*! Damage a falling block deals to bodies in the cell it lands in, per block fallen beyond
        the first (an anvil: 2, at most landingDamageLimit); 0 for none. Server side only. */
    double landingDamage = 0.0;
    /*! Largest landing damage of a falling block in total and largest \c {landingDamage}. */
    static constexpr double landingDamageLimit = 40.0;
    /*! Using the block opens its machine screen (furnace). */
    QcMachineDefinition machine;
    /*! Seconds the block's item burns as fuel in a machine; 0 for blocks that are no fuel. */
    double fuelSeconds = 0.0;
    /*! A sapling that grows into a tree feature (\c {"sapling"}); its \c {feature} is empty for
        other blocks. */
    QcSaplingDefinition sapling;
    /*! A monster spawner (decision E-520). Server side only. */
    QcSpawnerDefinition spawner;
    /*! Sound material ("qtcraft:stone"), empty for silent blocks, and explicit sound names per
        QcBlockSoundAction that replace the material's sound (see QcSoundKeys). \qcshared */
    QString soundMaterial;
    std::array<QString, qcBlockSoundActionCount> soundKeys;
    /*! Texture names indexed by QcBlockFace. */
    std::array<QString, qcBlockFaceCount> faceTextures;
    /*! Further named textures that shape parts refer to (door halves), as slot -> texture. */
    QList<std::pair<QString, QString>> textureSlots;
    /*! Item icon drawn flat instead of the block model (torches, doors); empty for the block
        model. */
    QString itemTexture;
    /*! How a hand holds the item that places this block, for the fields that item leaves unset
        (decision E-831). */
    QcHoldDisplay hold;
    /*! Whether the block gets an item of its own name; internal variants (a lit lamp, a piston
        head) have none and drop the item of their main block. Server side only. */
    bool hasItem = true;
    /*! Block light emitted, 0..15. */
    int lightLevel = 0;
    /*! Entities inside climb up and down (ladders). */
    bool climbable = false;
    /*! Using the block flips its \c {open} property (doors, trapdoors, gates). */
    bool toggles = false;
    /*! Two blocks tall: placed, opened and broken together; \c {half} tells bottom from top. */
    bool door = false;
    /*! Two blocks long: placed and broken together, the head in the \c {facing} direction of the
        foot; \c {part} tells foot from head. Villages count them (QcVillages). Server side only
        (not synchronised). */
    bool bed = false;
    /*! Stairs with corners (decision E-754): the states \c {facing}, \c {half} and \c {shape}
        (straight, inner_left, inner_right, outer_left, outer_right); the server sets \c {shape}
        from the stairs around it (QcStairShapes). Derived from the states, server side only. */
    bool stairs = false;
    /*! How the placed block's \c {facing} follows the player's view (\c {"facing"}). */
    QcFacingRule facingRule = QcFacingRule::Away;
    /*! What the block needs next to it to stay (\c {"support"}; cross-shaped blocks default to
        below). */
    QcBlockSupport support = QcBlockSupport::None;
    /*! Connection group this block belongs to and the groups whose members (and full solid
        blocks) set its north, east, south and west properties (fences, walls, panes)
        (\c {"connects"}: \c {"group"}, \c {"to"}; without \c {"to"}, its own group). \qcshared */
    QString connectGroup;
    QStringList connectsTo;
    /*! Whether full solid blocks count as connections (\c {"solids": false}: only the groups,
        tripwire, decision E-782). Server side only. */
    bool connectsToSolids = true;
    /*! Item slots of a storage block (chest); 0 for blocks without storage. */
    int containerSlots = 0;
    /*! Slots per row of its screen (a dispenser's 3 x 3, a hopper's row of 5). */
    int containerColumns = 9;
    /*! A portable container (\c {"portable": true}, a backpack, decision E-832): broken, it
        drops as one item that carries its contents; it holds no portable container. Its item
        gets QcItemDefinition::portableContainer. Server side only. */
    bool portableContainer = false;
    /*! Sounds of a storage block opening (its first viewer) and closing (its last one): a
        chest's lid, a barrel's cover, a backpack's flap (\c {"openSound"}, \c {"closeSound"},
        decision E-879); empty for silence. Server side only. \qcshared */
    QString containerOpenSound;
    QString containerCloseSound;
    /*! Tags of the item that places this block (server side only). */
    QStringList itemTags;
    /*! "<material>/<family>" for building parts made by QcPartGenerator, empty otherwise; reaches
        the client (the creative tab Building parts sorts by it, decision E-874). */
    QString partOf;
    /*! Flow of a liquid block; distance and movement factor also reach the client. */
    QcLiquidDefinition flow;
    /*! Damage per hit to bodies touching the block (lava); items inside burn unless
        \c {burnsItems} is false (a campfire hurts only creatures and players, decision E-772).
        Server side only. */
    double contactDamage = 0.0;
    /*! Whether items inside the block burn from its \c {contactDamage} (\c {"burnsItems"}). Server
        side only. */
    bool burnsItems = true;
    /*! Factor on the movement of bodies inside the block (cobweb; 1 = no effect). Reaches the
        client. */
    double slowsInside = 1.0;
    /*! Factors on walking speed and jump of bodies standing on the block (mud; \c {"surface"}:
        \c {"speed"}, \c {"jump"}, 0.1 to 2). Reach the client. \qcshared */
    double surfaceSpeed = 1.0;
    double surfaceJump = 1.0;
    /*! How slippery the block is to bodies on it, in the units of the original game (decision
        E-430): the default 0.6 grips, ice 0.98; above 0.6 bodies keep their momentum
        (QcEntityMotion::step). Reaches the client. */
    double slipperiness = grippingSlipperiness;
    /*! Touching a block named \c {hardensWith} (any state, e.g. water) turns this block into
        \c {hardensInto} (concrete powder into concrete). Server side only. \qcshared */
    QString hardensWith;
    QString hardensInto;
    /*! Placed on the surface of a liquid (a lily pad, decision E-774): the player aims at the
        liquid and the block goes into the cell above it. Reaches the client. */
    bool floats = false;
    /*! Eaten bite by bite where it stands (cake, decision E-773); the food reaches the client. */
    QcBitesDefinition bites;
    /*! A campfire (decision E-772); the slots reach the client. */
    QcCampfireDefinition campfire;
    /*! A liquid flowing into the block breaks it with its drops (plants, torches). Server side
        only. */
    bool washable = false;
    /*! Without a block below it falls as an entity and lands again (sand, gravel). Server side
        only. */
    bool falls = false;
    /*! Fire, flammability and eternal fire (decision E-392). Server side only. */
    QcFireDefinition fire;
    /*! Blast resistance and explosives (decision E-600). Server side only. */
    QcBlastDefinition blast;
    /*! A rail (decision E-382): minecarts roll along it; its states \c {facing} and \c {shape}
        follow from the rails around it (QcRails). Server side only. */
    bool rail = false;
    /*! What the rail does to carts (\c {"railAction"}: boost, activate); such rails have a
        \c {powered} state. */
    QcRailAction railAction = QcRailAction::None;
    /*! Its top carries blocks that need support below although the top face is not full (rails
        on a Signal Hopper, decision E-386). Server side only. */
    bool supportsAbove = false;
    /*! Blocks it may stand on (plants); empty: any full solid top face. Server side only. */
    QStringList groundBlocks;
    /*! A liquid that must touch the ground block from a side (sugar cane: water), unless the
        ground is this block itself. Server side only. */
    QString groundLiquid;
    /*! Growing upwards in a column (\c {"growth"}, sugar cane); needs support below. Server side
        only. */
    QcGrowthDefinition growth;
    /*! Ripening in stages (crops, stems) and being soil for them (farmland); server side only.
        \qcshared */
    QcCropDefinition crop;
    QcSoilDefinition soil;
    /*! Block it turns into when an item that \c {tills} (a hoe) is used on it, empty for none.
        Server side only. */
    QString tillsInto;
    /*! Block it turns into when an item that \c {flattens} (a shovel) is used on its top, empty
        for none (decision E-406). Server side only. */
    QString flattensInto;
    /*! Bone meal used on it grows grass and flowers of the biome around it (a grass block,
        decision E-814). Server side only. */
    bool growsVegetation = false;
    /*! Block it turns into when an item that \c {carves} (shears) is used on it, facing the
        clicked side, dropping \c {carveDropCount} of the item \c {carveDrop} (a pumpkin carved
        into a head, decision E-472); empty for none. Server side only. */
    QString carvesInto;
    /*! Item dropped when the block is carved (\c {"carving"}, \c {"drop"}), empty for none. Server
        side only. */
    QString carveDrop;
    /*! Number of \c {carveDrop} items dropped (\c {"count"}; 1 by default when there is a drop).
        Server side only. */
    int carveDropCount = 0;
    /*! Block it turns into, keeping the state properties both have, when an item that
        \c {strips} (an axe) is used on it (a log losing its bark, decision E-753); empty for
        none. Server side only. */
    QString strippedInto;
    /*! State property a dye sets to its colour when used on the block (QcItemDefinition::dye;
        the signal indicator light's \c {color}, decision E-765); empty for none. Server side
        only. */
    QString dyeProperty;
    /*! Block it turns into at its next block update while an opaque full block lies on it (a
        dirt path becoming dirt, decision E-406), empty for none. Server side only. */
    QString coveredInto;
    /*! Part in Signal Dust circuits (decision E-333); the kind reaches the client (usable
        blocks), the rest is server side only. */
    QcSignalDefinition signal;
    /*! A sign with text lines (\c {"sign"}); reaches the client. */
    QcSignDefinition sign;

    /*! Highest block light level (\c {lightLevel}). */
    static constexpr int maxLightLevel = 15;
    /*! Most item slots a storage block may have (\c {containerSlots}). */
    static constexpr int maxContainerSlots = 54;
    /*! Slipperiness of a block that grips (the default and the least a block may have). */
    static constexpr double grippingSlipperiness = 0.6;
    /*! The most slippery a block may be. */
    static constexpr double maxSlipperiness = 0.995;

    /*! Returns whether creatures keep from spawning on or in the block because it hurts or slows
        them (lava, cobweb; QcNaturalSpawner). */
    bool keepsSpawnsAway() const { return contactDamage > 0.0 || slowsInside < 1.0; }
    /*! Returns whether the block is water: a liquid that sets nothing alight (lava does); the one
        test for spawn spots in water, drowned and fish, putting out burning bodies and
        QcPhysics::isWaterAt. */
    bool isWater() const { return liquid && fire.setsAlightSeconds == 0; }
    /*! Returns whether the server gives the block random ticks (growing plants, crops, soil). */
    bool hasRandomTicks() const { return growth.grows() || crop.isCrop() || soil.isSoil(); }
    /*! Returns the texture name of the block's \a face. */
    const QString &texture(QcBlockFace face) const { return faceTextures[static_cast<std::size_t>(face)]; }
    /*! Returns the texture of the named \a slot, or an empty string. */
    QString slotTexture(const QString &slot) const
    {
        for (const auto &[slotName, slotTextureName] : textureSlots) {
            if (slotName == slot) {
                return slotTextureName;
            }
        }
        return {};
    }
    /*! Returns the state property named \a propertyName, or \c {nullptr} if the block has none. */
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
