#include "QcTextureSpec.h"

#include <QHash>

using namespace Qt::StringLiterals;

std::optional<QcTextureSpec::Pattern> qcTexturePatternFromName(const QString &name)
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
        {u"lava"_s, Pattern::Lava},         {u"flower"_s, Pattern::Flower},
        {u"tall_grass"_s, Pattern::TallGrass}, {u"mushroom"_s, Pattern::Mushroom},
        {u"sugar_cane"_s, Pattern::SugarCane}, {u"bottle"_s, Pattern::Bottle},
        {u"bucket"_s, Pattern::Bucket},     {u"lantern"_s, Pattern::Lantern},
        {u"limb"_s, Pattern::Limb},         {u"sleeve"_s, Pattern::Sleeve},
    };

    const auto it = patterns.constFind(name);
    if (it == patterns.constEnd()) {
        return std::nullopt;
    }
    return *it;
}
