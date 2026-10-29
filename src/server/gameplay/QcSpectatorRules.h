// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcVec3.h"
#include "common/protocol/QcMessages.h"

#include <optional>

class QcBlockAccess;
struct QcServerClient;

// What the server holds a spectator to (decision E-730) beyond the rules of the shared physics and
// the body that others do not see (QcServerClient::hasBody): which of the player's requests are
// dropped, and where a player who stops flying through blocks comes out.
namespace QcSpectatorRules {

// Whether the server carries out `message` from `client`: a player whose mode does not interact
// (QcGameModes::Rules::interacts) sends nothing that touches the world or the inventory (mining,
// using, attacking, trading, crafting, moving, dropping, throwing, catalogue items, picking
// blocks). Moving, looking, chat, choosing a hotbar slot and closing screens stay.
[[nodiscard]] bool allows(const QcServerClient &client, const QcMessage &message);

// Where a player whose body is stuck inside blocks after leaving spectator mode comes out: the
// lowest place straight above the feet where the body touches no block, within the world's height;
// the feet themselves when the body is free already; none when the column has no room (the player
// stays where he is).
[[nodiscard]] std::optional<QcVec3> freePlaceAbove(const QcBlockAccess &world, const QcVec3 &feet);

} // namespace QcSpectatorRules
