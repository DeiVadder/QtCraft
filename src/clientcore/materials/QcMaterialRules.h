#pragma once

#include "clientcore/materials/QcMaterialParams.h"

#include <QJsonObject>
#include <QString>

// The materials of the procedural textures (decision E-908): a surface by pattern (stone, wood,
// bricks, glass, metal …), refined by words of the texture's name (the metal of a block or an
// ore), light for the textures of light-emitting blocks, and finally the "material" object of the
// definition itself (a mod's texture or a pack's spec). Pure; the same input always gives the
// same material.
namespace QcMaterialRules {

// The material of the procedural texture `name` ("qtcraft:iron_ore") with the fields of
// `definition` (its pattern and an optional "material" object); `emitsLight` when a block with a
// light level uses it.
[[nodiscard]] QcMaterialParams forTexture(const QString &name, const QJsonObject &definition, bool emitsLight);

} // namespace QcMaterialRules
