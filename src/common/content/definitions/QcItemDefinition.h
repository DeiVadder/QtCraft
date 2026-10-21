#pragma once

#include "common/content/definitions/QcArmourDefinition.h"
#include "common/content/definitions/QcHangingDefinition.h"
#include "common/content/definitions/QcProjectileDefinition.h"

#include <QString>
#include <QStringList>

#include <algorithm>
#include <optional>

// Runtime item id: index into the registry's item table. 0 is "no item" (empty slot).
using QcItemId = quint32;

inline constexpr QcItemId qcNoItem = 0;

// What a Signal Dispenser does with the item (decision E-345). Anything else is thrown out like a
// dropper does; seeds are thrown and plant themselves on farmland (decision E-294), never planted.
enum class QcDispenseAction : quint8 {
    Throw,
    // A filled bucket pours its liquid (`places`) into the cell in front and becomes `emptiesTo`.
    PlaceLiquid,
    // An empty bucket (`scoops`) takes a liquid source in front.
    Scoop,
    // Armour goes onto a player or armour stand in front with that slot free.
    Equip,
    // Flint and steel (`ignites`) lights fire in the cell in front and wears by one (decision E-393).
    Ignite,
    // Ammunition (`projectile`) flies out of the front as that projectile (an arrow trap, decision
    // E-621).
    Shoot,
};

// A dial the item shows while it is held (decision E-769): a compass needle pointing to the world
// spawn, a clock face with the time of day.
enum class QcItemDial : quint8 {
    None,
    Compass,
    Clock,
};
inline constexpr int qcItemDialCount = 3;

// A map (decision E-804): an empty map becomes its filled map (`filledMap`) of the land around
// the player when used; a filled map shows the land of its map id, which its stacks carry in
// `QcItemStack::damage` (decision E-798).
enum class QcMapItem : quint8 {
    None,
    Empty,
    Filled,
};
inline constexpr int qcMapItemCount = 3;

// Drunk with the use button like food is eaten (milk, decision E-500); it becomes `emptiesTo`.
struct QcDrinkDefinition
{
    // Drinking it ends all status effects of the player (QcStatusEffects::clear).
    bool clearsEffects = false;

    bool operator==(const QcDrinkDefinition &) const = default;
};

struct QcItemDefinition
{
    // Largest allowed maxStack.
    static constexpr int maxStackLimit = 999;
    // Food points of a full player.
    static constexpr int maxFood = 20;

    QString name;
    QString translationKey;
    // Translation key of the handbook's text about it ("<translation>.description" unless set); a
    // missing text leaves only the facts. Server side only.
    QString descriptionKey;
    // Icon texture; block items use the block's top texture unless set.
    QString texture;
    int maxStack = 64;
    // Block placed when the item is used on a block face; empty for non-placeable items.
    QString placesBlock;
    // Tool category (e.g. "pickaxe") and tier; empty for non-tools.
    QString tool;
    int toolLevel = 0;
    // Mining speed multiplier for blocks of this tool category.
    double toolSpeed = 1.0;
    // Uses until a tool breaks; 0 for items without durability.
    int durability = 0;
    // Damage dealt when hitting with this item, in health points.
    double attackDamage = 1.0;
    // Food points restored when eaten (a full player has 20); 0 for items that are not food.
    int food = 0;
    // Seconds the item burns as fuel in a machine; 0 for items that are no fuel.
    double fuelSeconds = 0.0;
    // Groups the item belongs to ("qtcraft:planks"); recipes accept any member of "#group".
    QStringList tags;
    // Used on a liquid source, takes it and becomes the liquid's bucket item (empty bucket).
    bool scoops = false;
    // Used on a liquid, fills from it and becomes the liquid's bottle item, leaving the liquid
    // (glass bottle, decision E-211).
    bool bottles = false;
    // What the item turns into once it placed its block (a filled bucket becomes empty) or was
    // eaten (soup leaves its bowl).
    QString emptiesTo;
    // Worn in an equipment slot (decision E-234); items with armour stack to 1.
    std::optional<QcArmourDefinition> armour;
    // Used on the top of a block, places an armour stand that drops this item again (decision E-238).
    bool placesStand = false;
    // Used on a block face, hangs an item frame or a painting there that drops this item again
    // (decision E-790).
    std::optional<QcHangingDefinition> hangs;
    // Used on a rail, places a minecart that drops this item again (decision E-382). Server side only.
    bool placesCart = false;
    // The block the placed cart carries (decision E-386): a container block (a chest) makes a
    // container cart with the block's slots, a Signal Hopper a hopper cart; empty for a plain cart.
    // Reaches the client, which draws the block in the cart and names the cart's screen.
    QString cartBlock;
    // Used on a block with `tillsInto` (grass, dirt), turns it into that block (a hoe making farmland,
    // decision E-290). Server side only.
    bool tills = false;
    // Used on the top of a block with `flattensInto` (grass, dirt), turns it into that block (a
    // shovel making a dirt path, decision E-406). Server side only.
    bool flattens = false;
    // Used on a block with `carvesInto` (a pumpkin), carves it facing the clicked side and wears by one
    // (shears, decision E-472). Server side only.
    bool carves = false;
    // Used on a block with `strippedInto` (a log), turns it into that block and wears by one (axes,
    // decision E-753). Server side only.
    bool strips = false;
    // The colour a dye gives (`"dye": "red"`): used on a block with `dyeProperty` that has this
    // value, sets it and takes one item (the signal indicator light, decision E-765); empty for
    // none. Server side only.
    QString dye;
    // Used on a block face, lights fire in the cell in front of it and wears by one (flint and
    // steel, decision E-393). Also reaches the client (the creative inventory files it under tools).
    bool ignites = false;
    // Used on a crop, makes it grow by a few stages; on a sapling, lets it grow into its tree soon
    // with fertilizeSaplingChance (bone meal, decision E-426). Server side only.
    bool fertilizes = false;
    static constexpr double fertilizeSaplingChance = 0.45;
    // Server side only.
    QcDispenseAction dispense = QcDispenseAction::Throw;
    // Drawn with the use button and shooting ammunition (a bow, decision E-425); reaches the client,
    // which draws it while the button is held.
    std::optional<QcLauncherDefinition> launcher;
    // Ammunition: the projectile (QcProjectileDefinition) it becomes when a launcher shoots it;
    // empty when its kind is unknown (QcContentRegistry::finish). The name reaches the client,
    // which draws a launcher only with ammunition.
    QString projectile;
    // Thrown by hand with the use button as its projectile (a snowball, decision E-427); needs
    // `projectile`. Reaches the client (arm swing and cooldown); launchers without an ammunition
    // list do not shoot it.
    std::optional<QcThrowableDefinition> throwable;
    // Drunk with the use button (milk, decision E-500); reaches the client, which sends the use as
    // QcMsg::ConsumeItem.
    std::optional<QcDrinkDefinition> drink;
    // The dial the client shows while the item is held (`"dial": "compass"` or `"clock"`, decision
    // E-769); reaches the client.
    QcItemDial dial = QcItemDial::None;
    // An empty or filled map (`"map": "empty"` or `"filled"`, decision E-804); reaches the client.
    QcMapItem map = QcMapItem::None;
    // The item a filled map is zoomed out with in the crafting grid (`"zoomsWith"`, eight around
    // the map, decision E-804); empty for none. Server side only.
    QString zoomsWith;
    // The filled map an empty map becomes when used (`"filledMap"`, decision E-804). Server side
    // only.
    QString filledMap;
    // A creature that comes out where the item places its liquid (a fish bucket, decision E-501): kept
    // with the world like a bred animal. Server side only.
    QString releasesMob;
    // A fishing rod (decision E-503): the use button casts and reels in its bobber. Only that it fishes
    // reaches the client, which sends the use as QcMsg::ThrowItem.
    std::optional<QcFishingDefinition> fishing;

    // The `damage` a stack of this item may carry (decision E-798): worn uses below the durability,
    // the map id of a filled map (not negative), else 0.
    [[nodiscard]] int keptDamage(int damage) const
    {
        if (map == QcMapItem::Filled) {
            return std::max(0, damage);
        }
        return std::clamp(damage, 0, std::max(0, durability - 1));
    }

    // Leaves out what does not go together with the projectile uses (when the content is finished):
    // the use button throws a throwable item (QcClientLauncher::uses), so its other uses would never
    // happen (decision E-427), and an empty ammunition list means none. The warnings, one per change.
    [[nodiscard]] QStringList settleProjectileUses();
};
