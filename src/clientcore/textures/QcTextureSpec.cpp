#include "clientcore/textures/QcTextureSpec.h"

#include <QHash>

using namespace Qt::StringLiterals;

[[nodiscard]] std::optional<QcTextureSpec::Pattern> qcTexturePatternFromName(const QString &name)
{
    using Pattern = QcTextureSpec::Pattern;
    static const QHash<QString, Pattern> patterns = {
        {u"noise"_s, Pattern::Noise},       {u"speckle"_s, Pattern::Speckle},
        {u"planks"_s, Pattern::Planks},     {u"bricks"_s, Pattern::Bricks},
        {u"log_side"_s, Pattern::LogSide},  {u"log_top"_s, Pattern::LogTop},
        {u"leaves"_s, Pattern::Leaves},     {u"glass"_s, Pattern::Glass},
        {u"grass_side"_s, Pattern::GrassSide}, {u"ore"_s, Pattern::Ore},
        {u"water"_s, Pattern::Water},       {u"tool"_s, Pattern::Tool},
        {u"lump"_s, Pattern::Lump},         {u"pickaxe"_s, Pattern::Pickaxe},
        {u"axe"_s, Pattern::Axe},           {u"shovel"_s, Pattern::Shovel},
        {u"sword"_s, Pattern::Sword},       {u"stick"_s, Pattern::Stick},
        {u"ingot"_s, Pattern::Ingot},       {u"gem"_s, Pattern::Gem},
        {u"sapling"_s, Pattern::Sapling},
        {u"face"_s, Pattern::Face},         {u"furnace"_s, Pattern::Furnace},
        {u"torch"_s, Pattern::Torch},       {u"torch_item"_s, Pattern::TorchItem},
        {u"ladder"_s, Pattern::Ladder},     {u"door_lower"_s, Pattern::DoorLower},
        {u"door_upper"_s, Pattern::DoorUpper}, {u"trapdoor"_s, Pattern::Trapdoor},
        {u"chest_side"_s, Pattern::ChestSide}, {u"chest_front"_s, Pattern::ChestFront},
        {u"chest_top"_s, Pattern::ChestTop},
        {u"lava"_s, Pattern::Lava},         {u"fire"_s, Pattern::Fire},
        {u"flower"_s, Pattern::Flower},
        {u"tall_grass"_s, Pattern::TallGrass}, {u"mushroom"_s, Pattern::Mushroom},
        {u"sugar_cane"_s, Pattern::SugarCane}, {u"bottle"_s, Pattern::Bottle},
        {u"bucket"_s, Pattern::Bucket},     {u"lantern"_s, Pattern::Lantern},
        {u"limb"_s, Pattern::Limb},         {u"sleeve"_s, Pattern::Sleeve},
        {u"nugget"_s, Pattern::Nugget},     {u"metal_block"_s, Pattern::MetalBlock},
        {u"bars"_s, Pattern::Bars},         {u"bookshelf"_s, Pattern::Bookshelf},
        {u"paper"_s, Pattern::Paper},       {u"book"_s, Pattern::Book},
        {u"bowl"_s, Pattern::Bowl},         {u"stew"_s, Pattern::Stew},
        {u"apple"_s, Pattern::Apple},       {u"leather"_s, Pattern::Leather},
        {u"scrap"_s, Pattern::Scrap},       {u"string"_s, Pattern::String},
        {u"cobweb"_s, Pattern::Cobweb},     {u"dead_bush"_s, Pattern::DeadBush},
        {u"fern"_s, Pattern::Fern},         {u"firefly_glass"_s, Pattern::FireflyGlass},
        {u"barrel_side"_s, Pattern::BarrelSide}, {u"barrel_top"_s, Pattern::BarrelTop},
        {u"stained_glass"_s, Pattern::StainedGlass},
        {u"helmet"_s, Pattern::Helmet},     {u"chestplate"_s, Pattern::Chestplate},
        {u"leggings"_s, Pattern::Leggings}, {u"boots"_s, Pattern::Boots},
        {u"armour_plate"_s, Pattern::ArmourPlate}, {u"armour_leather"_s, Pattern::ArmourLeather},
        {u"helmet_front"_s, Pattern::HelmetFront}, {u"visor"_s, Pattern::Visor},
        {u"armour_trim"_s, Pattern::ArmourTrim}, {u"armor_stand"_s, Pattern::ArmourStand},
        {u"emblem"_s, Pattern::Emblem},
        {u"hoe"_s, Pattern::Hoe},           {u"crop"_s, Pattern::Crop},
        {u"root_crop"_s, Pattern::RootCrop}, {u"stem"_s, Pattern::Stem},
        {u"farmland"_s, Pattern::Farmland}, {u"pumpkin_side"_s, Pattern::PumpkinSide},
        {u"pumpkin_top"_s, Pattern::PumpkinTop}, {u"melon_side"_s, Pattern::MelonSide},
        {u"wheat"_s, Pattern::Wheat},       {u"bread"_s, Pattern::Bread},
        {u"carrot"_s, Pattern::Carrot},     {u"melon_slice"_s, Pattern::MelonSlice},
        {u"pie"_s, Pattern::Pie},           {u"hay_side"_s, Pattern::HaySide},
        {u"hay_top"_s, Pattern::HayTop},    {u"flint_and_steel"_s, Pattern::FlintAndSteel},
        {u"dust"_s, Pattern::Dust},         {u"lever"_s, Pattern::Lever},
        {u"repeater"_s, Pattern::Repeater}, {u"piston_side"_s, Pattern::PistonSide},
        {u"hopper"_s, Pattern::Hopper},     {u"hopper_top"_s, Pattern::HopperTop},
        {u"hopper_item"_s, Pattern::HopperItem}, {u"comparator"_s, Pattern::Comparator},
        {u"observer_face"_s, Pattern::ObserverFace}, {u"observer_back"_s, Pattern::ObserverBack},
        {u"observer_side"_s, Pattern::ObserverSide}, {u"dispenser"_s, Pattern::Dispenser},
        {u"dropper"_s, Pattern::Dropper},   {u"rail"_s, Pattern::Rail},
        {u"minecart"_s, Pattern::Minecart}, {u"bow"_s, Pattern::Bow},
        {u"arrow"_s, Pattern::Arrow},       {u"feather"_s, Pattern::Feather},
        {u"bone"_s, Pattern::Bone},         {u"fishing_rod"_s, Pattern::FishingRod},
        {u"carved_pumpkin"_s, Pattern::CarvedPumpkin}, {u"shears"_s, Pattern::Shears},
    };

    const auto it = patterns.constFind(name);
    if (it == patterns.constEnd()) {
        return std::nullopt;
    }
    return *it;
}
