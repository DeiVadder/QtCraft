// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"

#include <QJsonObject>

struct QcJsonFieldReader;

/*! \brief The textures of a block: one per face plus the named slots that shape parts use. */
struct QcBlockTextures
{
    /*! Texture name per face, indexed by QcBlockFace. */
    std::array<QString, qcBlockFaceCount> faces;
    /*! Named slots as slot -> texture name. */
    QList<std::pair<QString, QString>> namedSlots;
};

/*! \brief Parses the "textures" object of visible blocks ("all", "side", "top", "bottom", face
    names and named "slots") and checks that every shape part names a known slot (decision
    E-765). */
namespace QcBlockTextureParser {

/*! Returns the face textures and slots of the block JSON \a blockJson, names qualified in
    \a packNamespace; failures go to \a reader. */
[[nodiscard]] QcBlockTextures parseTextures(const QJsonObject &blockJson, QcJsonFieldReader &reader, const QString &packNamespace);

/*! Fails \a reader when a shape part of the visible \a block uses a slot the block does not
    define. */
void checkPartTextures(const QcBlockDefinition &block, QcJsonFieldReader &reader);

} // namespace QcBlockTextureParser
