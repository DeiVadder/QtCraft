#include "QcItemIcons.h"

#include "QcBlockIcon.h"
#include "QcBlockRenderTable.h"
#include "QcContentRegistry.h"
#include "QcItemIconProvider.h"
#include "QcRenderBridge.h"
#include "QcTextureAtlas.h"

#include <QHash>
#include <QImage>

#include <array>

void QcItemIcons::draw(const QcContentRegistry &content, const QcTextureAtlas &atlas, const QcBlockRenderTable *renderTable)
{
    m_ready = true;
    if (!m_provider) {
        return;
    }

    QHash<QString, QImage> icons;
    const std::array<QImage, 6> shaded = QcBlockIcon::shadedAtlases(atlas.image());
    for (const QcItemDefinition &item : content.items()) {
        // Blocks show their shape, unless they bring a flat icon (torches, doors) or are plants.
        const std::optional<QcBlockId> block = content.blockId(item.placesBlock);
        const bool model = block && renderTable && content.block(*block).itemTexture.isEmpty()
                           && content.block(*block).renderLayer != QcRenderLayer::Invisible
                           && !content.state(*block).boxes.isEmpty();
        icons.insert(item.name, model ? QcBlockIcon::render(renderTable->entry(QcRenderBridge::itemState(content, *block)), shaded)
                                      : atlas.tile(item.texture));
    }
    m_provider->setIcons(std::move(icons));
}
