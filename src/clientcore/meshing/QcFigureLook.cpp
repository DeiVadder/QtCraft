#include "QcFigureLook.h"

#include "QcContentRegistry.h"
#include "QcLightVolume.h"

using namespace Qt::StringLiterals;

namespace QcFigureLook {

QcEntityMesher::ModelLook player(const QcContentRegistry &content, const QcAppearance &appearance)
{
    QcEntityMesher::ModelLook look;
    look.model = content.model(u"qtcraft:humanoid"_s);
    look.outfit = content.appearance(appearance.outfit);
    if (!look.outfit || look.outfit->kind != QcAppearanceDefinition::Kind::Outfit) {
        look.outfit = content.appearance(QcAppearance().outfit);
    }
    look.hair = content.appearance(appearance.hair);
    if (look.hair && look.hair->kind != QcAppearanceDefinition::Kind::Hair) {
        look.hair = nullptr;
    }
    look.colors = appearance;
    return look;
}

QcBlockId itemState(const QcContentRegistry &content, QcBlockId block)
{
    if (!content.block(block).connectsTo.isEmpty()) {
        block = content.withProperty(content.withProperty(block, u"east"_s, u"true"_s), u"west"_s, u"true"_s);
    }
    return block;
}

QcEntityMesher::ItemLook item(const QcContentRegistry &content, QcItemId item)
{
    QcEntityMesher::ItemLook look;
    if (!content.isValidItem(item)) {
        return look;
    }

    const QcItemDefinition &definition = content.item(item);
    look.sprite = definition.texture;
    const std::optional<QcBlockId> block = content.blockId(definition.placesBlock);
    if (block) {
        look.glow = float(content.block(*block).lightLevel) / float(QcLightVolume::maxLevel);
    }
    if (block && content.block(*block).itemTexture.isEmpty() && !content.state(*block).boxes.isEmpty()
        && content.block(*block).renderLayer != QcRenderLayer::Invisible) {
        look.cube = true;
        look.faces = content.block(*block).faceTextures;
        if (!content.state(itemState(content, *block)).fullCube) {
            look.boxes = content.state(itemState(content, *block)).boxes;
        }
    }
    return look;
}

std::optional<QcEntityMesher::ItemLook> held(const QcContentRegistry &content, QcItemId item)
{
    if (item == qcNoItem || !content.isValidItem(item)) {
        return std::nullopt;
    }
    return QcFigureLook::item(content, item);
}

} // namespace QcFigureLook
