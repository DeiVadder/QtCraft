// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/icons/QcItemIconSet.h"

#include "clientcore/figures/QcFigureLook.h"
#include "clientcore/icons/QcBlockIcon.h"
#include "clientcore/meshing/QcBlockRenderTable.h"
#include "clientcore/textures/QcTextureAtlas.h"
#include "common/content/registry/QcContentRegistry.h"

#include <array>

using namespace Qt::StringLiterals;

namespace QcItemIconSet {

namespace {

// The icon of the block item placing `block`: a bed with both its halves, head behind foot, as it
// is two cells long (decision E-885); every other block as its item state.
QImage blockIcon(const QcContentRegistry &content, const QcBlockRenderTable &renderTable, QcBlockId block, const std::array<QImage, 6> &shaded, int size)
{
    const QcBlockId state = QcFigureLook::itemState(content, block);
    if (!content.block(block).bed) {
        return QcBlockIcon::render(renderTable.entry(state), shaded, size);
    }

    // Facing west, the head lies one cell west of the foot: at the back of the icon.
    const QcBlockId west = content.withProperty(state, u"facing"_s, u"west"_s);
    const QcBlockId foot = content.withProperty(west, u"part"_s, u"foot"_s);
    const QcBlockId head = content.withProperty(west, u"part"_s, u"head"_s);
    // A bed without halves (a mod's, no `part` property) draws its one state.
    if (head == foot) {
        return QcBlockIcon::render(renderTable.entry(state), shaded, size);
    }
    const QList<QcBlockIcon::Part> parts{{&renderTable.entry(head), {0, 0, 0}}, {&renderTable.entry(foot), {1, 0, 0}}};
    return QcBlockIcon::render(parts, shaded, size);
}

} // namespace

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
        icons.insert(item.name, model ? blockIcon(content, *renderTable, *block, shaded, size) : atlas.tile(item.texture, QcBlockIcon::largeIconSize));
    }
    return icons;
}

} // namespace QcItemIconSet
