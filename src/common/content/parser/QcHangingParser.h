#pragma once

#include "common/content/definitions/QcHangingDefinition.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

class QcAssetCatalog;
struct QcJsonFieldReader;

// Parses the "hangs" object of an item (decision E-790): "type" ("item_frame" or "painting"), for a
// painting its "motifs" ({"name", "texture", "width", "height"}, sizes 1 to 4) and its "back" texture.
namespace QcHangingParser {
// Failures go to `reader`; names get `packNamespace`.
QcHangingDefinition parseHanging(const QJsonObject &json, const QString &packNamespace, QcJsonFieldReader &reader);
// What is wrong with the textures of the painting `item` once every pack is loaded (decision
// E-791): a motif of one block needs its texture, a larger one a procedural texture whose "tiles"
// are exactly its width and height. One line per motif in error.
[[nodiscard]] QStringList checkTextures(const QString &item, const QcHangingDefinition &hangs, const QcAssetCatalog &assets);
} // namespace QcHangingParser
