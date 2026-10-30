// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/mobs/QcModelDefinition.h"

#include <QList>
#include <QString>

/*! \brief Colour a texture is multiplied with: one of the player's chosen colours, or none.

    Collar is the collar of a tame creature (only on creature model parts, QcMobPart::tint;
    decision E-480), which travels in the shirt colour of the creature's appearance. */
enum class QcTint : quint8 {
    None, // the texture as it is
    Skin, // the skin colour
    Hair, // the hair colour
    Shirt, // the shirt colour
    Pants, // the pants colour
    Collar, // a tame creature's collar colour
};

/*! \brief Texture for one slot of a model. */
struct QcAppearanceLayer
{
    /*! Slot the texture covers (the key in \c {"layers"}). */
    QString slot;
    /*! Content name of the texture (\c {"texture"}). */
    QString texture;
    /*! Colour the texture is multiplied with (\c {"tint"}: none, skin, hair, shirt or pants). */
    QcTint tint = QcTint::None;
};

/*! \brief Size of a cape in metres and its distance from the back (decision E-240).

    The plate hangs from the top of the torso's back; its upper square part (as long as it is
    wide) carries the layer "emblem", the rest the layer "outside"; "inside" faces the back,
    "edge" the narrow sides. */
struct QcCapeShape
{
    /*! Smallest width and length the data may set. */
    static constexpr double minSize = 0.05;
    /*! Largest width the data may set. */
    static constexpr double maxWidth = 1.0;
    /*! Largest length the data may set. */
    static constexpr double maxLength = 1.6;
    /*! Largest thickness the data may set. */
    static constexpr double maxThickness = 0.2;
    /*! Largest offset the data may set. */
    static constexpr double maxOffset = 0.3;

    /*! Width across the back (\c {"width"}). */
    double width = 0.6;
    /*! Length from the top of the torso down (\c {"length"}). */
    double length = 0.95;
    /*! Thickness of the plate (\c {"thickness"}, at least 0.005). */
    double thickness = 0.04;
    /*! Gap between the torso's back and the cape (armour layers add their own clearance). */
    double offset = 0.02;

    bool operator==(const QcCapeShape &) const = default;
};

/*! \brief An outfit (textures for the slots of a skeleton model), a hair style (extra boxes on the
    head, textured through its own layers) or a cape (a plate on the back, textured through its
    layers).

    Players pick one of each; creatures name an outfit and optionally a cape. */
struct QcAppearanceDefinition
{
    /*! What it is (\c {"kind"}: outfit, hair or cape). */
    enum class Kind : quint8 {
        Outfit, // textures for the slots of a skeleton model
        Hair, // extra boxes on the head
        Cape // a plate on the back
    };

    /*! Most layers of an appearance. */
    static constexpr int maxLayers = 64;
    /*! Most boxes of a hair style. */
    static constexpr int maxBoxes = 64;

    /*! Content name (\c {"name"}). */
    QString name;
    /*! Translation key of its name (\c {"translation"}, else derived from the name). */
    QString translationKey;
    /*! What it is. */
    Kind kind = Kind::Outfit;
    /*! False for creature outfits: not offered to players and refused by the server. */
    bool playable = true;
    /*! Textures by slot (\c {"layers"}), 1 to maxLayers; a cape needs the layer "outside". */
    QList<QcAppearanceLayer> layers;
    /*! Hair only: boxes that turn with the head bone; their slots refer to \c {layers}. */
    QList<QcModelBox> boxes;
    /*! Cape only. */
    QcCapeShape cape;

    /*! Returns the layer for \a slot, or \c {nullptr} if it has none. */
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
