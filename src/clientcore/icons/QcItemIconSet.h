// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QImage>
#include <QString>

class QcBlockRenderTable;
class QcContentRegistry;
class QcTextureAtlas;

/*! \brief The icon of every item of a content registry, keyed by item name: blocks drawn as their
    shape when a render table is given, everything else (and blocks with a flat icon, plants) as its
    texture; and the icon of every creature (QcMobIcon) under QcHandbookLinks::mobIcon(name).

    The game shows them through QcItemIcons, the web handbook export packs them into one sheet. */
namespace QcItemIconSet {

/*! Returns the icons of the items and creatures of \a content drawn from \a atlas; blocks with a
    shape use \a renderTable (null: all flat). \a blockIconSize 0 picks the size the game uses:
    QcBlockIcon::iconSize, or largeIconSize for texture packs above 16 x 16 pixels. */
QHash<QString, QImage> draw(const QcContentRegistry &content, const QcTextureAtlas &atlas, const QcBlockRenderTable *renderTable,
                            int blockIconSize = 0);

} // namespace QcItemIconSet
