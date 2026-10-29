// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/meshing/QcSpriteShapes.h"

#include "clientcore/textures/QcTextureAtlas.h"

std::shared_ptr<const QcSpriteShape> QcSpriteShapes::shape(const std::shared_ptr<const QcTextureAtlas> &atlas, const QString &texture)
{
    if (!atlas) {
        return nullptr;
    }
    // An expired atlas never equals a live one, even at the same address.
    if (m_atlas.lock() != atlas) {
        m_atlas = atlas;
        m_shapes.clear();
    }

    const auto found = m_shapes.constFind(texture);
    if (found != m_shapes.constEnd()) {
        return *found;
    }
    return *m_shapes.insert(texture, std::make_shared<const QcSpriteShape>(QcSpriteShape::fromImage(atlas->tile(texture, maxTexels))));
}
