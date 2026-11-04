// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/entitymeshes/QcSpriteShape.h"

#include <QHash>
#include <QString>

#include <memory>

class QcTextureAtlas;

/*! \brief The shapes of item sprites in a texture atlas (decision E-831), each cut from the
    atlas on first use, at the atlas's own resolution up to maxTexels so that sides and picture
    agree (decision E-991), and kept until the atlas changes. */
class QcSpriteShapes
{
public:
    /*! The most texels along an edge a shape is cut at: larger tiles take the mip level of this
        size, so a 256 pixel pack does not build meshes of 256 texels (decision E-991). */
    static constexpr int maxTexels = 128;

    /*! Returns the shape of \a texture in \a atlas; null without an atlas. Another atlas than
        the last call's forgets the shapes kept so far. */
    [[nodiscard]] std::shared_ptr<const QcSpriteShape> shape(const std::shared_ptr<const QcTextureAtlas> &atlas, const QString &texture);

private:
    std::weak_ptr<const QcTextureAtlas> m_atlas;
    QHash<QString, std::shared_ptr<const QcSpriteShape>> m_shapes;
};
