#pragma once

#include <QHash>
#include <QImage>
#include <QString>

class QcBlockRenderTable;
class QcContentRegistry;
class QcTextureAtlas;

// The icon of every item of a content registry, keyed by item name: blocks drawn as their shape when
// a render table is given, everything else (and blocks with a flat icon, plants) as its texture.
// The game shows them through QcItemIcons, the web handbook export packs them into one sheet.
namespace QcItemIconSet {

// `blockIconSize` 0 picks the size the game uses: QcBlockIcon::iconSize, or largeIconSize for
// texture packs above 16 x 16 pixels.
QHash<QString, QImage> draw(const QcContentRegistry &content, const QcTextureAtlas &atlas, const QcBlockRenderTable *renderTable,
                            int blockIconSize = 0);

} // namespace QcItemIconSet
