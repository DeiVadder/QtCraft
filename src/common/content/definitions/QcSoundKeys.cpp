#include "QcSoundKeys.h"

#include <array>

using namespace Qt::StringLiterals;

namespace QcSoundKeys {

QString actionName(QcBlockSoundAction action)
{
    static const std::array<QString, qcBlockSoundActionCount> names = {u"break"_s, u"place"_s, u"step"_s, u"hit"_s};
    return names[std::size_t(action)];
}

QString defaultMaterialFor(const QcBlockDefinition &block)
{
    if (block.renderLayer == QcRenderLayer::Invisible || block.liquid) {
        return {};
    }
    if (block.shapeName == u"qtcraft:cross"_s || block.renderLayer == QcRenderLayer::Cutout) {
        return u"qtcraft:grass"_s;
    }
    if (block.tool == u"axe"_s) {
        return u"qtcraft:wood"_s;
    }
    if (block.tool == u"shovel"_s) {
        return u"qtcraft:dirt"_s;
    }
    if (block.renderLayer == QcRenderLayer::Translucent) {
        return u"qtcraft:glass"_s;
    }
    return QString::fromLatin1(defaultMaterial);
}

QString blockSound(const QcBlockDefinition &block, QcBlockSoundAction action)
{
    const QString &explicitKey = block.soundKeys[std::size_t(action)];
    if (!explicitKey.isEmpty()) {
        return explicitKey;
    }

    const qsizetype colon = block.soundMaterial.indexOf(u':');
    if (colon <= 0) {
        return {};
    }
    return u"%1:block.%2.%3"_s.arg(block.soundMaterial.left(colon), block.soundMaterial.mid(colon + 1), actionName(action));
}

} // namespace QcSoundKeys
