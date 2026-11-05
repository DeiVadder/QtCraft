// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/textures/QcTexturePaint.h"
#include "common/core/QcResult.h"

#include <QJsonObject>

#include <optional>

/*! \brief The pattern "emblem" (decision E-241): pixel art from a small bitmap in the content data,
    on woven cloth in the primary colour.

    Cape emblems are drawn this way, so the motif is data and a mod can exchange it without code. */
namespace QcTextureEmblem {

/*! Returns the colour of pixel (\a x, \a y) of \a spec: the palette's colour for the bitmap's
    character there, else cloth in \a color. */
QColor paint(const QcTextureSpec &spec, int x, int y, QColor color);
/*! Returns the pattern "icon": the palette's colour for the character of the bitmap of \a spec at
    (\a x, \a y), else transparent; \a color is unused. */
QColor paintIcon(const QcTextureSpec &spec, int x, int y, QColor color);

/*! Returns \a spec with "bitmap" (an array of strings) and "palette" (character -> "#rrggbb") read
    from \a json, a texture definition; the problem when they are malformed. */
[[nodiscard]] QcResult<QcTextureSpec> read(const QJsonObject &json, QcTextureSpec spec);

} // namespace QcTextureEmblem
