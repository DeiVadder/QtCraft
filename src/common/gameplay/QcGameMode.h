#pragma once

#include <QLatin1StringView>
#include <QStringView>
#include <QtGlobal>

#include <optional>

// The game mode of a player (decision E-282), a server state per player that the client mirrors.
// The values are stable: they are saved in player files and sent in the protocol. Adventure (2)
// and spectator (3) are not implemented; their numbers stay free so they can follow without a
// change of the formats.
enum class QcGameMode : quint8 {
    Survival = 0,
    Creative = 1,
};

namespace QcGameModes {

inline constexpr int count = 2;

// What a mode changes about the rules. Server and client derive everything from the mode, so the
// protocol carries only the mode itself.
struct Rules
{
    // Double-tapping jump toggles flight (shared physics, QcPhysics::stepPlayer).
    bool mayFly = false;
    // Blocks break with the first strike, also unbreakable ones, and drop nothing.
    bool instantBreak = false;
    // Placing a block or using an item does not use up the held stack.
    bool infiniteItems = false;
    // No damage except /kill; creatures do not target the player.
    bool invulnerable = false;
    // Food points and exhaustion stay as they are.
    bool hungerFrozen = false;
    // Items can be taken from the catalogue of all items (QcMsg::CreativeItem).
    bool itemCatalogue = false;
};

Rules rules(QcGameMode mode);

// Players of modes with the same inventory set share their inventory; changing to a mode of
// another set keeps the current inventory aside and brings back the one of that set (decision
// E-283). Survival is set 0, creative set 1.
inline constexpr int inventorySetCount = 2;
int inventorySet(QcGameMode mode);

// "survival", "creative": the names of commands, settings files and world.json.
QLatin1StringView name(QcGameMode mode);
// The name, a short form ("s", "c") or the number ("0", "1"), in any case; nullopt otherwise.
std::optional<QcGameMode> parse(QStringView text);
// A value read from a file or the network; nullopt for unknown values.
std::optional<QcGameMode> fromValue(quint8 value);

} // namespace QcGameModes
