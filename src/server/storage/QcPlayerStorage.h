#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcResult.h"
#include "common/gameplay/QcEquipment.h"
#include "common/gameplay/QcExperience.h"
#include "common/gameplay/QcGameMode.h"
#include "common/gameplay/QcInventory.h"
#include "common/gameplay/QcStatusEffects.h"
#include "common/physics/QcEntityMotion.h"

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
    // Level and points (decision E-398).
    QcExperience experience;
    // The enchantment seed (decision E-864); 0 in files of version 6.
    quint32 enchantSeed = 0;
    // Status effects (decision E-450).
    QList<QcStatusEffect> effects;
};

// The player files of a world directory (players/<name>-<hash>.qcp, decision E-093): position,
// view, inventory and armour, health and food, the game mode with the kept inventory of the other
// mode, the experience. Items are stored by name; unknown items are dropped with a warning.
class QcPlayerStorage
{
public:
    QcPlayerStorage(QString directory, std::shared_ptr<const QcContentRegistry> content);

    // File of a player in players/: the name in lower case (other characters than letters, digits,
    // '_' and '-' replaced) and a hash of the exact name, "<name>-<hash>.qcp".
    static QString fileName(const QString &playerName);
    // A player as read, with the warnings of reading his file (unknown items dropped, an unknown game
    // mode played as survival).
    struct Read
    {
        // std::nullopt: the player has never been saved.
        std::optional<QcPlayerData> player;
        QStringList warnings;
    };

    // A player file that cannot be read: why, and the warnings of reading it up to there.
    struct Failure
    {
        QString error;
        QStringList warnings;
    };
    using Result = std::expected<Read, Failure>;

    // A file of an older development version is an error (decision E-356).
    [[nodiscard]] Result read(const QString &playerName) const;
    [[nodiscard]] QcStatus write(const QString &playerName, const QcPlayerData &player) const;
    // The file of a player relative to the world directory ("players/<file name>").
    static QString relativePath(const QString &playerName);
    // The contents of a player file, for a save that writes it with the world (decision E-378).
    QByteArray bytes(const QcPlayerData &player) const;
    // A player from the contents of his file (none: never saved).
    [[nodiscard]] Result parse(const QString &playerName, const std::optional<QByteArray> &contents) const;

private:
    void writeSlots(QDataStream &stream, const QcInventory &inventory) const;
    void writeEquipment(QDataStream &stream, const QcEquipment &equipment) const;
    // Whether the read values are finite and inside the world.
    static bool isPlausible(const QcPlayerData &player);
    QString path(const QString &playerName) const;

    QString m_directory;
    std::shared_ptr<const QcContentRegistry> m_content;
};
