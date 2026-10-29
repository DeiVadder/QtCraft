// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/mobs/QcModelDefinition.h"
#include "common/core/QcVec3.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

// The four armour slots of a player or armour stand (decisions E-232, E-234).
enum class QcEquipmentSlot : quint8 { Head, Chest, Legs, Feet };

// A layer box of a piece of armour (decision E-235): every box of the reference model (the
// humanoid) whose slot is in `covers`, made `inflate` metres larger on every side and cut to the
// heights `fromY`..`toY` (model space), drawn with `texture` on its bone. `front` textures the
// face looking forward (-Z) instead, e.g. a helmet with an opening for the face.
struct QcArmourLayer
{
    QStringList covers;
    QString texture;
    QString front;
    double inflate = 0.03;
    double fromY = -8.0;
    double toY = 8.0;

    bool operator==(const QcArmourLayer &) const = default;
};

// An extra box of a piece of armour on one bone (shoulder piece, crest, visor, toe cap), in model
// space like the bone's own boxes.
struct QcArmourPart
{
    QcBoneRole bone = QcBoneRole::Body;
    QcVec3 from;
    QcVec3 to;
    QString texture;
    QString front;

    bool operator==(const QcArmourPart &) const = default;
};

// What makes an item armour: the slot it is worn in, the armour points it gives, the set it
// belongs to (showcase stands and the handbook group by it) and how it is drawn on a body.
struct QcArmourDefinition
{
    static constexpr int maxPoints = 20;
    static constexpr int maxLayers = 8;
    static constexpr int maxParts = 16;
    static constexpr int maxCovers = 16;
    static constexpr double maxInflate = 0.2;

    QcEquipmentSlot slot = QcEquipmentSlot::Head;
    int points = 0;
    QString set;
    QList<QcArmourLayer> layers;
    QList<QcArmourPart> parts;

    bool operator==(const QcArmourDefinition &) const = default;
};

// Slot names used in data, commands and saved files: "head", "chest", "legs", "feet".
QString qcEquipmentSlotName(QcEquipmentSlot slot);
[[nodiscard]] std::optional<QcEquipmentSlot> qcEquipmentSlotFromName(const QString &name);
