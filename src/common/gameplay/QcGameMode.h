// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QLatin1StringView>
#include <QStringView>
#include <QtGlobal>

#include <optional>

/*! \brief The game mode of a player (decision E-282), a server state per player that the client
    mirrors.

    The values are stable: they are saved in player files and sent in the protocol. */
enum class QcGameMode : quint8 {
    /*! The normal rules: health, hunger, mining and building with what one has. */
    Survival = 0,
    /*! Flying, instant breaking, unlimited items from the catalogue, no damage and no hunger. */
    Creative = 1,
    /*! Survival without breaking and placing except where the world's allow-lists permit it
        (decision E-735, QcAdventureRules). */
    Adventure = 2,
    /*! Flies through blocks, unseen and without touching anything (decision E-730). */
    Spectator = 3,
};

/*! \brief How a game mode lets a player move (QcEntityMotion::stepPlayer): on foot, flying on a
    wish (creative, decision E-284) or flying through blocks all the time (spectator, decision
    E-730). */
enum class QcMovement : quint8 {
    /*! On foot. */
    Walk,
    /*! May fly on a wish. */
    MayFly,
    /*! Flies through blocks all the time. */
    NoClip
};

/*! \brief The rules of each game mode, and its names. */
namespace QcGameModes {

/*! The number of modes; their values are 0 to count - 1. */
inline constexpr int count = 4;

/*! \brief What a mode changes about the rules.

    Server and client derive everything from the mode, so the protocol carries only the mode
    itself. */
struct Rules
{
    /*! Double-tapping jump toggles flight (shared physics, QcEntityMotion::stepPlayer). */
    bool mayFly = false;
    /*! Blocks break with the first strike, also unbreakable ones, and drop nothing. */
    bool instantBreak = false;
    /*! Placing a block or using an item does not use up the held stack. */
    bool infiniteItems = false;
    /*! No damage except /kill; creatures do not target the player. */
    bool invulnerable = false;
    /*! Food points and exhaustion stay as they are. */
    bool hungerFrozen = false;
    /*! Items can be taken from the catalogue of all items (QcMsg::CreativeItem). */
    bool itemCatalogue = false;
    /*! Flies through blocks all the time (decision E-730): no collision, gravity, liquids or
        ladders, the speed set with the mouse wheel (QcPlayerInput::flySpeed). */
    bool noClip = false;
    /*! The player's own actions: breaks, places and uses blocks and items, attacks, trades,
        opens screens, picks up items and changes the inventory; without it the player only looks
        (spectator). Commands are no such action: they stay an operator's privilege in every
        mode. */
    bool interacts = true;
    /*! The body is in the world: other players see it, creatures notice it, projectiles,
        minecarts and placed blocks meet it, it picks up nothing without it and the void hurts
        it. Without it the player is invisible and untouchable (spectator). */
    bool tangible = true;
    /*! Blocks break and items act on blocks only where the world's adventure allow-lists permit
        it (QcAdventureRules::permits); using blocks, creatures and items in the air stays as in
        survival. */
    bool buildByAllowList = false;

    /*! Returns how the rules let a player move. */
    [[nodiscard]] QcMovement movement() const { return noClip ? QcMovement::NoClip : (mayFly ? QcMovement::MayFly : QcMovement::Walk); }
};

/*! Returns the rules of \a mode. */
Rules rules(QcGameMode mode);

/*! Players of modes with the same inventory set share their inventory; changing to a mode of
    another set keeps the current inventory aside and brings back the one of that set (decision
    E-283). Survival and adventure are set 0, creative set 1; spectator keeps the survival set
    (it uses none). */
inline constexpr int inventorySetCount = 2;
/*! Returns the inventory set of \a mode (see inventorySetCount). */
int inventorySet(QcGameMode mode);

/*! Returns the name of \a mode: "survival", "creative", "adventure", "spectator", the names of
    commands, settings files and world.json. */
QLatin1StringView name(QcGameMode mode);
/*! Returns the mode \a text names: the name, a short form ("s", "c", "a", "sp") or the number
    ("0", "1", "2", "3"), in any case; nullopt otherwise. */
[[nodiscard]] std::optional<QcGameMode> parse(QStringView text);
/*! Returns the mode of \a value, read from a file or the network; nullopt for unknown values. */
[[nodiscard]] std::optional<QcGameMode> fromValue(quint8 value);

} // namespace QcGameModes
