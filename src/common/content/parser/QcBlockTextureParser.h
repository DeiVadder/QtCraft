#pragma once

#include "common/content/definitions/QcBlockDefinition.h"

#include <QJsonObject>

struct QcJsonFieldReader;

// The textures of a block: one per face plus the named slots that shape parts use.
struct QcBlockTextures
{
    std::array<QString, qcBlockFaceCount> faces;
    QList<std::pair<QString, QString>> namedSlots;
};

// Parses the "textures" object of visible blocks ("all", "side", "top", "bottom", face names and
// named "slots") and checks that every shape part names a known slot (decision E-765).
namespace QcBlockTextureParser {

// The face textures and slots of the block JSON `blockJson`; failures go to `reader`.
[[nodiscard]] QcBlockTextures parseTextures(const QJsonObject &blockJson, QcJsonFieldReader &reader, const QString &packNamespace);

// Fails `reader` when a shape part of the visible `block` uses a slot the block does not define.
void checkPartTextures(const QcBlockDefinition &block, QcJsonFieldReader &reader);

} // namespace QcBlockTextureParser
