// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/definitions/QcExperienceDrop.h"
#include "common/core/QcVec3.h"

#include <QtGlobal>

class QcServerSounds;
struct QcServerClient;
class QcServerState;

// Experience in the server tick (decision E-398): orbs move, merge and despawn
// (QcExperienceOrbManager), players collect them with a sound, a level gained plays the level-up
// sound, and every player whose experience changed gets it (QcMsg::PlayerExperience) at the end of
// the step.
//
// The sources drop orbs through drop(): mined ores and killed creatures (QcExperienceDrop of their
// definition), furnaces when a player takes their output, bred animals, a dying player. Later
// spenders and givers (trading, enchanting, the anvil) change QcServerClient::experience directly
// (QcExperience::spendLevels, addLevels) or call givePoints(); the change reaches the client by itself.
class QcServerExperience
{
public:
    QcServerExperience(QcServerState &state, QcServerSounds &sounds);

    // One step: orbs and collecting, then the changed experience to the players.
    void tick();
    // Adds points to a player (a collected orb, a trade); a gained level plays the level-up sound.
    void givePoints(QcServerClient &client, qint64 points);
    // Sends the experience of every playing player whose experience changed since he last got it.
    void sendChanges();

    // Drops `points` as orbs at `position` (no orbs for 0 or less).
    static void drop(QcServerState &state, int points, const QcVec3 &position);
    // The points of an experience drop, chosen evenly between its bounds with the world's random.
    static int roll(QcServerState &state, QcExperienceDrop range);
    // Whole points of gathered experience (a furnace's): the fraction becomes one more point with its
    // own chance, as in the reference.
    static int takeWhole(QcServerState &state, double experience);

private:
    // A player collected an orb of `points`: its sound and the points.
    void collected(quint32 entityId, int points);

    QcServerState &m_state;
    QcServerSounds &m_sounds;
};
