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
    };
    const auto it = patterns.constFind(name);
    if (it == patterns.constEnd()) {
        return std::nullopt;
    }
    return *it;
}
