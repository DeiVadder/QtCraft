#pragma once

#include <QString>

struct QcCreature;
struct QcServerClient;
class QcServerState;

// Who owns tame creatures (decision E-480): a player's persistent identity rather than an entity id,
// which changes with every login.
namespace QcPetOwners {

// The owner key of a player: the fingerprint of the identity a remote player proved at the login
// (decision E-252), else "name:" and the case-folded name (the local player of the in-process host,
// who logs in without a key; a name belongs to one identity on a server, E-252).
QString keyOf(const QcServerClient &client);

// True when the creature is a pet of another player than the one with entity id `player` (0 for
// none: false): players never hurt the pets of others, nor do their pets attack them (like the
// reference without fights between players).
bool isOthersPet(const QcServerState &state, const QcCreature &mob, quint32 player);

} // namespace QcPetOwners
