#pragma once

#include "common/content/definitions/QcModelDefinition.h"

#include <QList>
#include <QString>

// Colour a texture is multiplied with: one of the player's chosen colours, or none. Collar is the
// collar of a tame creature (only on creature model parts, QcMobPart::tint; decision E-480), which
// travels in the shirt colour of the creature's appearance.
enum class QcTint : quint8 {
    None,
    Skin,
    Hair,
    Shirt,
    Pants,
    Collar,
};

// Texture for one slot of a model.
struct QcAppearanceLayer
{
    QString slot;
    QString texture;
    QcTint tint = QcTint::None;
};

// Size of a cape in metres and its distance from the back (decision E-240). The plate hangs from
// the top of the torso's back; its upper square part (as long as it is wide) carries the layer
// "emblem", the rest the layer "outside"; "inside" faces the back, "edge" the narrow sides.
struct QcCapeShape
{
    static constexpr double minSize = 0.05;
    static constexpr double maxWidth = 1.0;
    static constexpr double maxLength = 1.6;
    static constexpr double maxThickness = 0.2;
    static constexpr double maxOffset = 0.3;

    double width = 0.6;
    double length = 0.95;
    double thickness = 0.04;
    // Gap between the torso's back and the cape (armour layers add their own clearance).
    double offset = 0.02;

    bool operator==(const QcCapeShape &) const = default;
};

// An outfit (textures for the slots of a skeleton model), a hair style (extra boxes on the head,
// textured through its own layers) or a cape (a plate on the back, textured through its layers).
// Players pick one of each; creatures name an outfit and optionally a cape.
struct QcAppearanceDefinition
{
    enum class Kind : quint8 { Outfit, Hair, Cape };

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
    // Cape only.
    QcCapeShape cape;

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
