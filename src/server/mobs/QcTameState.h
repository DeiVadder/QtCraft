#pragma once

#include <QString>

// What a tame creature is (QcMobDefinition::tameable, decision E-480): whose it is, whether it sits
// and the colour of its collar, saved with it (component Tame of QcEntityCodec); and whom it attacks
// for its owner now, which is not saved.
struct QcTameState
{
    // The identity of its owner (QcPetOwners::keyOf): the fingerprint of a player's proven identity,
    // or "name:" and the case-folded name of a player without one (the local player). Never an
    // entity id; the owner may be offline.
    QString owner;
    // Sits where it is: it neither follows nor teleports nor fights.
    bool sitting = false;
    // The RGB colour of its collar (kinds with a collar, QcTameDefinition::hasCollar).
    quint32 collar = 0;
    // Entity id of the creature it attacks for its owner (0: none) and seconds until it gives up on
    // it; not saved.
    quint32 target = 0;
    double targetTimer = 0.0;
    // The server tick of the owner's fight that set the target (PlayerView::hurtTick or struckTick):
    // only a newer fight sets another; not saved.
    quint64 targetTick = 0;

    bool operator==(const QcTameState &) const = default;
};
