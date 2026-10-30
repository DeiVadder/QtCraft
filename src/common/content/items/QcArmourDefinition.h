// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/mobs/QcModelDefinition.h"
#include "common/core/QcVec3.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

/*! \brief The four armour slots of a player or armour stand (decisions E-232, E-234). */
enum class QcEquipmentSlot : quint8 {
    Head, // Helmet slot, data name "head".
    Chest, // Chestplate slot, data name "chest".
    Legs, // Leggings slot, data name "legs".
    Feet // Boots slot, data name "feet".
};

/*! \brief A layer box of a piece of armour (decision E-235): every box of the reference model (the
    humanoid) whose slot is in \c {covers}, made \c {inflate} metres larger on every side and cut to
    the heights \c {fromY}..\c {toY} (model space), drawn with \c {texture} on its bone.

    \c {front} textures the face looking forward (-Z) instead, e.g. a helmet with an opening for
    the face. */
struct QcArmourLayer
{
    /*! Model slot names whose boxes the layer copies (\c {"covers"}, 1 to
        QcArmourDefinition::maxCovers names). */
    QStringList covers;
    /*! Qualified texture name of the layer (\c {"texture"}, required). */
    QString texture;
    /*! Qualified texture for the face looking forward (\c {"front"}); empty when not set. */
    QString front;
    /*! How much larger the boxes get on every side, in metres (\c {"inflate"}, 0 to
        QcArmourDefinition::maxInflate). */
    double inflate = 0.03;
    /*! Lower cut height in model space (\c {"fromY"}, -8 to 8, below \c {toY}). */
    double fromY = -8.0;
    /*! Upper cut height in model space (\c {"toY"}, -8 to 8). */
    double toY = 8.0;

    bool operator==(const QcArmourLayer &) const = default;
};

/*! \brief An extra box of a piece of armour on one bone (shoulder piece, crest, visor, toe cap),
    in model space like the bone's own boxes. */
struct QcArmourPart
{
    /*! Bone the box moves with (\c {"bone"}, default \c {"body"}). */
    QcBoneRole bone = QcBoneRole::Body;
    /*! Lower corner of the box (\c {"from"}, smaller than \c {to} on every axis). */
    QcVec3 from;
    /*! Upper corner of the box (\c {"to"}). */
    QcVec3 to;
    /*! Qualified texture name of the box (\c {"texture"}, required). */
    QString texture;
    /*! Qualified texture for the face looking forward (\c {"front"}); empty when not set. */
    QString front;

    bool operator==(const QcArmourPart &) const = default;
};

/*! \brief What makes an item armour: the slot it is worn in, the armour points it gives, the set
    it belongs to (showcase stands and the handbook group by it) and how it is drawn on a body. */
struct QcArmourDefinition
{
    /*! Upper limit of \c {points}. */
    static constexpr int maxPoints = 20;
    /*! Most entries in \c {layers}. */
    static constexpr int maxLayers = 8;
    /*! Most entries in \c {parts}. */
    static constexpr int maxParts = 16;
    /*! Most slot names in QcArmourLayer::covers. */
    static constexpr int maxCovers = 16;
    /*! Upper limit of QcArmourLayer::inflate, in metres. */
    static constexpr double maxInflate = 0.2;

    /*! Slot the item is worn in (\c {"slot"}, required). */
    QcEquipmentSlot slot = QcEquipmentSlot::Head;
    /*! Armour points it gives (\c {"points"}, 0 to maxPoints, default 0). */
    int points = 0;
    /*! Qualified name of the set (\c {"set"}); defaults to the item's own name. */
    QString set;
    /*! Layer boxes copied from the humanoid model (\c {"layers"}). */
    QList<QcArmourLayer> layers;
    /*! Extra boxes on single bones (\c {"parts"}). */
    QList<QcArmourPart> parts;

    bool operator==(const QcArmourDefinition &) const = default;
};

/*! Returns the name of \a slot used in data, commands and saved files: "head", "chest", "legs",
    "feet". */
QString qcEquipmentSlotName(QcEquipmentSlot slot);
/*! Returns the slot whose name (see qcEquipmentSlotName()) is \a name, or std::nullopt for any
    other text. */
[[nodiscard]] std::optional<QcEquipmentSlot> qcEquipmentSlotFromName(const QString &name);
