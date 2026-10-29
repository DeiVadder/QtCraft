// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/animals/QcPetOwners.h"

#include "server/core/QcServerClient.h"
#include "server/core/QcServerState.h"
#include "server/entities/QcCreature.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace QcPetOwners {

QString keyOf(const QcServerClient &client)
{
    return client.login.fingerprint.isEmpty() ? u"name:"_s + client.name.toCaseFolded() : client.login.fingerprint;
}

bool isOthersPet(const QcServerState &state, const QcCreature &mob, quint32 player)
{
    if (player == 0 || !mob.isTame()) {
        return false;
    }
    const auto client = std::ranges::find_if(state.clients(), [&](const auto &candidate) { return candidate->id == player; });
    return client != state.clients().end() && keyOf(**client) != mob.tame->owner;
}

} // namespace QcPetOwners
