#include "clientcore/textures/QcTextureSpec.h"

#include <QHash>
#include <QStringView>

#include <utility>

using namespace Qt::StringLiterals;

namespace {

using Pattern = QcTextureSpec::Pattern;

// The names of the patterns in texture definitions (content/qtcraft/textures and texture packs).
constexpr std::pair<QStringView, Pattern> patternNames[] = {
    {u"noise", Pattern::Noise},       {u"speckle", Pattern::Speckle},
    {u"planks", Pattern::Planks},     {u"bricks", Pattern::Bricks},
    {u"log_side", Pattern::LogSide},  {u"log_top", Pattern::LogTop},
    {u"leaves", Pattern::Leaves},     {u"glass", Pattern::Glass},
    {u"grass_side", Pattern::GrassSide}, {u"ore", Pattern::Ore},
    {u"water", Pattern::Water},       {u"tool", Pattern::Tool},
    {u"lump", Pattern::Lump},         {u"pickaxe", Pattern::Pickaxe},
    {u"axe", Pattern::Axe},           {u"shovel", Pattern::Shovel},
    {u"sword", Pattern::Sword},       {u"stick", Pattern::Stick},
    {u"ingot", Pattern::Ingot},       {u"gem", Pattern::Gem},
    {u"sapling", Pattern::Sapling},
    {u"face", Pattern::Face},         {u"furnace", Pattern::Furnace},
    {u"torch", Pattern::Torch},       {u"torch_item", Pattern::TorchItem},
    {u"ladder", Pattern::Ladder},     {u"door_lower", Pattern::DoorLower},
    {u"door_upper", Pattern::DoorUpper}, {u"trapdoor", Pattern::Trapdoor},
    {u"chest_side", Pattern::ChestSide}, {u"chest_front", Pattern::ChestFront},
    {u"chest_top", Pattern::ChestTop},
    {u"lava", Pattern::Lava},         {u"fire", Pattern::Fire},
    {u"flower", Pattern::Flower},
    {u"tall_grass", Pattern::TallGrass}, {u"mushroom", Pattern::Mushroom},
    {u"sugar_cane", Pattern::SugarCane}, {u"bottle", Pattern::Bottle},
    {u"bucket", Pattern::Bucket},     {u"lantern", Pattern::Lantern},
    {u"limb", Pattern::Limb},         {u"sleeve", Pattern::Sleeve},
    {u"nugget", Pattern::Nugget},     {u"metal_block", Pattern::MetalBlock},
    {u"bars", Pattern::Bars},         {u"bookshelf", Pattern::Bookshelf},
    {u"paper", Pattern::Paper},       {u"book", Pattern::Book},
    {u"bowl", Pattern::Bowl},         {u"stew", Pattern::Stew},
    {u"apple", Pattern::Apple},       {u"leather", Pattern::Leather},
    {u"scrap", Pattern::Scrap},       {u"string", Pattern::String},
    {u"cobweb", Pattern::Cobweb},     {u"dead_bush", Pattern::DeadBush},
    {u"fern", Pattern::Fern},         {u"firefly_glass", Pattern::FireflyGlass},
    {u"barrel_side", Pattern::BarrelSide}, {u"barrel_top", Pattern::BarrelTop},
    {u"stained_glass", Pattern::StainedGlass},
    {u"raw_chunk", Pattern::RawChunk}, {u"raw_block", Pattern::RawBlock},
    {u"chain", Pattern::Chain},       {u"chain_across", Pattern::ChainAcross},
    {u"helmet", Pattern::Helmet},     {u"chestplate", Pattern::Chestplate},
    {u"leggings", Pattern::Leggings}, {u"boots", Pattern::Boots},
    {u"armour_plate", Pattern::ArmourPlate}, {u"armour_leather", Pattern::ArmourLeather},
    {u"helmet_front", Pattern::HelmetFront}, {u"visor", Pattern::Visor},
    {u"armour_trim", Pattern::ArmourTrim}, {u"armor_stand", Pattern::ArmourStand},
    {u"emblem", Pattern::Emblem},
    {u"hoe", Pattern::Hoe},           {u"crop", Pattern::Crop},
    {u"root_crop", Pattern::RootCrop}, {u"stem", Pattern::Stem},
    {u"farmland", Pattern::Farmland}, {u"pumpkin_side", Pattern::PumpkinSide},
    {u"pumpkin_top", Pattern::PumpkinTop}, {u"melon_side", Pattern::MelonSide},
    {u"wheat", Pattern::Wheat},       {u"bread", Pattern::Bread},
    {u"carrot", Pattern::Carrot},     {u"melon_slice", Pattern::MelonSlice},
    {u"pie", Pattern::Pie},           {u"hay_side", Pattern::HaySide},
    {u"hay_top", Pattern::HayTop},    {u"flint_and_steel", Pattern::FlintAndSteel},
    {u"dust", Pattern::Dust},         {u"lever", Pattern::Lever},
    {u"repeater", Pattern::Repeater}, {u"piston_side", Pattern::PistonSide},
    {u"hopper", Pattern::Hopper},     {u"hopper_top", Pattern::HopperTop},
    {u"hopper_item", Pattern::HopperItem}, {u"comparator", Pattern::Comparator},
    {u"observer_face", Pattern::ObserverFace}, {u"observer_back", Pattern::ObserverBack},
    {u"observer_side", Pattern::ObserverSide}, {u"dispenser", Pattern::Dispenser},
    {u"dropper", Pattern::Dropper},   {u"rail", Pattern::Rail},
    {u"minecart", Pattern::Minecart}, {u"bow", Pattern::Bow},
    {u"arrow", Pattern::Arrow},       {u"feather", Pattern::Feather},
    {u"bone", Pattern::Bone},         {u"fishing_rod", Pattern::FishingRod},
    {u"carved_pumpkin", Pattern::CarvedPumpkin}, {u"shears", Pattern::Shears},
    {u"mossy_bricks", Pattern::MossyBricks}, {u"cracked_bricks", Pattern::CrackedBricks},
    {u"chiseled_bricks", Pattern::ChiseledBricks}, {u"smooth_stone", Pattern::SmoothStone},
    {u"glazed_terracotta", Pattern::GlazedTerracotta}, {u"icon", Pattern::Icon},
    {u"cake_top", Pattern::CakeTop}, {u"cake_side", Pattern::CakeSide},
    {u"cake_item", Pattern::CakeItem}, {u"campfire_item", Pattern::CampfireItem},
    {u"lily_pad", Pattern::LilyPad},
};

} // namespace

[[nodiscard]] std::optional<QcTextureSpec::Pattern> qcTexturePatternFromName(const QString &name)
{
    static const QHash<QString, Pattern> patterns = [] {
        QHash<QString, Pattern> byName;
        for (const auto &[patternName, pattern] : patternNames) {
            byName.insert(patternName.toString(), pattern);
        }
        return byName;
    }();

    const auto it = patterns.constFind(name);
    if (it == patterns.constEnd()) {
        return std::nullopt;
    }
    return *it;
}
