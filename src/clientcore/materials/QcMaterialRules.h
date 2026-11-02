// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/materials/QcMaterialParams.h"

#include <QJsonObject>
#include <QString>

/*! \brief The materials of the procedural textures (decision E-908): a surface by pattern (stone,
    wood, bricks, glass, metal …), refined by words of the texture's name (the metal of a block or
    an ore), light for the textures of light-emitting blocks, and finally the "material" object of
    the definition itself (a mod's texture or a pack's spec).

    Pure; the same input always gives the same material. */
namespace QcMaterialRules {

/*! Returns the material of the procedural texture \a name ("qtcraft:iron_ore") with the fields
    of \a definition (its pattern and an optional "material" object); \a emitsLight when a block
    with a light level uses it. */
[[nodiscard]] QcMaterialParams forTexture(const QString &name, const QJsonObject &definition, bool emitsLight);

} // namespace QcMaterialRules
