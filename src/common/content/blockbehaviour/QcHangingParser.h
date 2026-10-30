// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blockbehaviour/QcHangingDefinition.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

class QcAssetCatalog;
struct QcJsonFieldReader;

/*! \brief Parses the "hangs" object of an item (decision E-790): "type" ("item_frame" or
    "painting"), for a painting its "motifs" ({"name", "texture", "width", "height"}, sizes 1 to
    4) and its "back" texture. */
namespace QcHangingParser {
/*! Returns the hanging definition of the "hangs" object \a json. Failures go to \a reader; names
    get \a packNamespace. */
QcHangingDefinition parseHanging(const QJsonObject &json, const QString &packNamespace, QcJsonFieldReader &reader);
/*! Returns what is wrong with the textures of the motifs \a hangs of the painting \a item, looked
    up in \a assets once every pack is loaded (decision E-791): a motif of one block needs its
    texture, a larger one a procedural texture whose "tiles" are exactly its width and height.
    One line per motif in error. */
[[nodiscard]] QStringList checkTextures(const QString &item, const QcHangingDefinition &hangs, const QcAssetCatalog &assets);
} // namespace QcHangingParser
