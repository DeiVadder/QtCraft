#pragma once

#include "QcContentRegistry.h"
#include "QcEntityMotion.h"
#include "QcEquipment.h"
#include "QcGameMode.h"
#include "QcInventory.h"
#include "QcResult.h"

#include <QDataStream>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>

struct QcPlayerData
{
    QcEntityMotion motion;
    float yaw = 0.0f;
    float pitch = 0.0f;
    QcInventory inventory;
    double health = 20.0;
    // Players saved before hunger existed come back full.
    int food = QcItemDefinition::maxFood;
    double exhaustion = 0.0;
    // Armour slots (decision E-234).
    QcEquipment equipment;
    // The game mode, and the inventory and armour of the other inventory set, kept aside while the
    // player is in a mode of the other set (decisions E-282, E-283).
    QcGameMode gameMode = QcGameMode::Survival;
    QcInventory keptInventory;
    QcEquipment keptEquipment;
};

// The player files of a world directory (players/<name>-<hash>.qcp, decision E-093): position,
// view, inventory and armour, health and food, the game mode with the kept inventory of the other
// mode. Items are stored by name; unknown items are dropped with a warning.
class QcPlayerStorage
{
public:
    QcPlayerStorage(QString directory, std::shared_ptr<const QcContentRegistry> content);

    // File of a player in players/: the name in lower case (other characters than letters, digits,
    // '_' and '-' replaced) and a hash of the exact name, "<name>-<hash>.qcp".
    static QString fileName(const QString &playerName);
    // std::nullopt without error when the player has never been saved; a file of an older development
    // version is an error (decision E-356).
    QcResult<std::optional<QcPlayerData>> read(const QString &playerName, QStringList &warnings) const;
    QcStatus write(const QString &playerName, const QcPlayerData &player) const;

private:
    void writeSlots(QDataStream &stream, const QcInventory &inventory) const;
    void writeEquipment(QDataStream &stream, const QcEquipment &equipment) const;
    // Inventory slots after the selected slot; false when they are damaged.
    bool readSlots(QDataStream &stream, const QString &path, QcInventory &inventory, QStringList &warnings) const;
    // The armour slots; pieces that no longer fit their slot go into `inventory`. False when damaged.
    bool readEquipment(QDataStream &stream, const QString &path, QcEquipment &equipment, QcInventory &inventory,
                       QStringList &warnings) const;
    // Game mode, flight and the kept inventory set.
    bool readGameMode(QDataStream &stream, const QString &path, QcPlayerData &player, QStringList &warnings) const;
    // Whether the read values are finite and inside the world.
    static bool isPlausible(const QcPlayerData &player);
    QString path(const QString &playerName) const;

    QString m_directory;
    std::shared_ptr<const QcContentRegistry> m_content;
};
