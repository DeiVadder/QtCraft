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
        {u"lump"_s, Pattern::Lump},
    };
    const auto it = patterns.constFind(name);
    if (it == patterns.constEnd()) {
        return std::nullopt;
    }
    return *it;
}
