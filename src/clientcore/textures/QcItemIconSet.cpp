#include "QcItemIconSet.h"

#include "QcBlockIcon.h"
#include "QcBlockRenderTable.h"
#include "QcContentRegistry.h"
#include "QcFigureLook.h"
#include "QcTextureAtlas.h"

#include <array>

namespace QcItemIconSet {

QHash<QString, QImage> draw(const QcContentRegistry &content, const QcTextureAtlas &atlas, const QcBlockRenderTable *renderTable,
                            int blockIconSize)
{
    QHash<QString, QImage> icons;
    // Icons are shown at most 64 pixels large: drawn from a small mip level, so a large texture pack
    // does not copy its whole atlas three times (decision E-300).
    const std::array<QImage, 6> shaded = QcBlockIcon::shadedAtlases(atlas.levelForTile(QcBlockIcon::sourceTileSize));
    const int defaultSize = atlas.tileSize() > 16 ? QcBlockIcon::largeIconSize : QcBlockIcon::iconSize;
    const int size = blockIconSize > 0 ? blockIconSize : defaultSize;
    for (const QcItemDefinition &item : content.items()) {
        // Blocks show their shape, unless they bring a flat icon (torches, doors) or are plants.
        const std::optional<QcBlockId> block = content.blockId(item.placesBlock);
        const bool model = block && renderTable && content.block(*block).itemTexture.isEmpty()
                           && content.block(*block).renderLayer != QcRenderLayer::Invisible
                           && !content.state(*block).boxes.isEmpty();
        icons.insert(item.name, model ? QcBlockIcon::render(renderTable->entry(QcFigureLook::itemState(content, *block)), shaded, size)
                                      : atlas.tile(item.texture, QcBlockIcon::largeIconSize));
    }
    return icons;
}

} // namespace QcItemIconSet
