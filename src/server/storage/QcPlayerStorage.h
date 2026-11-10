// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/items/QcItemDefinition.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/content/worldgen/QcDimensionDefinition.h"
#include "common/core/QcLoadFailure.h"
#include "common/core/QcResult.h"
#include "common/gameplay/QcAdvancementProgress.h"
#include "common/gameplay/QcAirSupply.h"
#include "common/gameplay/QcEquipment.h"
#include "common/gameplay/QcExperience.h"
#include "common/gameplay/QcGameMode.h"
#include "common/gameplay/QcInventory.h"
#include "common/gameplay/QcStatusEffects.h"
#include "common/physics/QcEntityMotion.h"
#include "server/storage/QcPlayerReturn.h"

#include <QDataStream>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>

/*! \brief What is saved of a player (decision E-093): his body, view, inventory, armour, health,
    food, game mode, experience, seeds, effects, advancements, air and dimension. */
struct QcPlayerData
{
    /*! Where his feet are and how he moves. */
    QcEntityMotion motion;
    /*! The view's yaw and pitch in degrees. \qcshared */
    float yaw = 0.0f;
    float pitch = 0.0f;
    /*! The hotbar and the main inventory. */
    QcInventory inventory;
    /*! Health points. */
    double health = 20.0;
    /*! Food and exhaustion; players saved before hunger existed come back full. \qcshared */
    int food = QcItemDefinition::maxFood;
    double exhaustion = 0.0;
    /*! The hidden saturation (decision E-1001); the reference's 5 of a new player in files before
        version 12. */
    double saturation = QcItemDefinition::spawnSaturation;
    /*! Armour slots (decision E-234). */
    QcEquipment equipment;
    /*! The game mode, and the inventory and armour of the other inventory set, kept aside while
        the player is in a mode of the other set (decisions E-282, E-283). \qcshared */
    QcGameMode gameMode = QcGameMode::Survival;
    QcInventory keptInventory;
    QcEquipment keptEquipment;
    /*! Level and points (decision E-398). */
    QcExperience experience;
    /*! The enchantment seed (decision E-864); 0 in files of version 6. */
    quint32 enchantSeed = 0;
    /*! The rune seed of the enchanting screen (decision E-884); 0 in files of versions 6 and 7. */
    quint32 runeSeed = 0;
    /*! Status effects (decision E-450). */
    QList<QcStatusEffect> effects;
    /*! The extra health left of the absorption effect (decision E-992). */
    double absorption = 0.0;
    /*! The met criteria of the adventure guide (decision E-923), unknown advancements included;
        empty in files before version 9. */
    QcAdvancementProgress advancements;
    /*! The air under water in game ticks (decision E-955); a full breath in files before version
        10. */
    int air = QcAirSupply::maxAir;
    /*! The dimension he is in (decision E-978); the overworld in files before version 11. A
        dimension the world's content does not have (a removed mod) is kept in the file until he
        is placed elsewhere. */
    QString dimension = QString::fromLatin1(QcDimensionDefinition::overworldName);
    /*! Where he returns to when that dimension can open again (decision E-989). */
    std::optional<QcPlayerReturn> returnTo;
    /*! The soul anchor he respawns at (decision E-1112); none in files before version 13. */
    std::optional<QcRespawnAnchor> anchor;
};

/*! \brief The player files of a world directory (players/<name>-<hash>.qcp, decision E-093):
    position, view, inventory and armour, health and food, the game mode with the kept inventory of
    the other mode, the experience, the advancements.

    Items are stored by name; unknown items are dropped with a warning. */
class QcPlayerStorage
{
public:
    QcPlayerStorage(QString directory, std::shared_ptr<const QcContentRegistry> content);

    /*! Returns the file of the player \a playerName in players/: the name in lower case (other
        characters than letters, digits, '_' and '-' replaced) and a hash of the exact name,
        "<name>-<hash>.qcp". */
    static QString fileName(const QString &playerName);
    /*! \brief A player as read, with the warnings of reading his file (unknown items dropped, an
        unknown game mode played as survival). */
    struct Read
    {
        /*! std::nullopt: the player has never been saved. */
        std::optional<QcPlayerData> player;
        /*! The warnings of reading. */
        QStringList warnings;
    };

    /*! A player as read, or why the file cannot be read. */
    using Result = std::expected<Read, QcLoadFailure>;

    /*! Returns the player \a playerName as read from his file. A file of an older development
        version is an error (decision E-356). */
    [[nodiscard]] Result read(const QString &playerName) const;
    /*! Writes \a player to the file of \a playerName. Returns an error when it cannot be
        written. */
    [[nodiscard]] QcStatus write(const QString &playerName, const QcPlayerData &player) const;
    /*! Returns the file of the player \a playerName relative to the world directory
        ("players/<file name>"). */
    static QString relativePath(const QString &playerName);
    /*! Returns the contents of the file of \a player, for a save that writes it with the world
        (decision E-378). */
    QByteArray bytes(const QcPlayerData &player) const;
    /*! Returns the player \a playerName read from \a contents of his file (none: never saved). */
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
