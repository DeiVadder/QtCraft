#pragma once

#include "clientcore/meshing/QcSpriteShape.h"

#include <QHash>
#include <QString>

#include <memory>

class QcTextureAtlas;

// The shapes of item sprites in a texture atlas (decision E-831), each cut from the atlas on first
// use and kept until the atlas changes.
class QcSpriteShapes
{
public:
    // Shapes have at most this many texels along an edge (the base resolution); higher resolutions
    // are read from a smaller mip level.
    static constexpr int maxTexels = 16;

    // The shape of `texture` in `atlas`; null without an atlas. Another atlas than the last call's
    // forgets the shapes kept so far.
    [[nodiscard]] std::shared_ptr<const QcSpriteShape> shape(const std::shared_ptr<const QcTextureAtlas> &atlas, const QString &texture);

private:
    std::weak_ptr<const QcTextureAtlas> m_atlas;
    QHash<QString, std::shared_ptr<const QcSpriteShape>> m_shapes;
};
