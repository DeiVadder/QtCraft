#pragma once

#include "QcContentRegistry.h"

#include <QHash>
#include <QImage>
#include <QRectF>

// Packs all content textures into one image. Procedural textures are synthesized, PNG textures
// decoded and scaled to the tile size. Unknown or broken textures map to a magenta placeholder.
class QcTextureAtlas
{
public:
    static constexpr auto missingTextureName = "qtcraft:missing";

    static QcTextureAtlas build(const QcContentRegistry &content);

    const QImage &image() const { return m_image; }
    // UV rectangle in [0, 1]; falls back to the placeholder for unknown names.
    QRectF uv(const QString &textureName) const;
    bool contains(const QString &textureName) const { return m_uvs.contains(textureName); }
    // Single texture tile, e.g. for item icons in the UI.
    QImage tile(const QString &textureName) const;
    QStringList problems() const { return m_problems; }

private:
    QImage m_image;
    QHash<QString, QRectF> m_uvs;
    QStringList m_problems;
};
