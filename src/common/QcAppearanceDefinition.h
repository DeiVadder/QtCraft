#pragma once

#include "QcModelDefinition.h"

#include <QList>
#include <QString>

// Colour a texture is multiplied with: one of the player's chosen colours, or none.
enum class QcTint : quint8 {
    None,
    Skin,
    Hair,
    Shirt,
    Pants,
};

// Texture for one slot of a model.
struct QcAppearanceLayer
{
    QString slot;
    QString texture;
    QcTint tint = QcTint::None;
};

// An outfit (textures for the slots of a skeleton model) or a hair style (extra boxes on the head,
// textured through its own layers). Players pick one of each; creatures name an outfit.
struct QcAppearanceDefinition
{
    enum class Kind : quint8 { Outfit, Hair };

    static constexpr int maxLayers = 64;
    static constexpr int maxBoxes = 64;

    QString name;
    QString translationKey;
    Kind kind = Kind::Outfit;
    // False for creature outfits: not offered to players and refused by the server.
    bool playable = true;
    QList<QcAppearanceLayer> layers;
    // Hair only: boxes that turn with the head bone; their slots refer to `layers`.
    QList<QcModelBox> boxes;

    const QcAppearanceLayer *layer(const QString &slot) const
    {
        for (const QcAppearanceLayer &entry : layers) {
            if (entry.slot == slot) {
                return &entry;
            }
        }
        return nullptr;
    }
};
