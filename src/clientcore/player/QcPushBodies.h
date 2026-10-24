#pragma once

#include "common/core/QcVec3.h"
#include "common/protocol/QcMessages.h"

#include <QHash>

class QcContentRegistry;

// The creatures whose bodies push the local player (decision E-882), as the tick core of the session
// knows them (QcSessionCore): the latest place and pose the server sent for each, not the
// interpolated past the entity mirror shows (QcClientEntities). The server pushes the player with
// the creatures as they stand after its last tick when his input arrives; the client's newest
// places are about a round trip older than that. So each creature is moved on along its last step
// (its move between the last two places the server sent; it sends every visible creature in every
// tick) for as many ticks as the input leads, at most maxLeadTicks and maxLead: for creatures that
// stand still the prediction pushes exactly as the server does, for walking ones it misses only by
// how much their step changed during the round trip.
class QcPushBodies
{
public:
    // The most ticks and metres a creature is moved on along its last step.
    static constexpr int maxLeadTicks = 10;
    static constexpr double maxLead = 1.0;

    // Takes over the spawns, moves and removals of creatures; ignores every other message.
    void apply(const QcMessage &message);
    // The push on the local player with his feet at `feet` (a velocity, QcBodyPush::playerVelocity)
    // from the creatures known, with their sizes from `content`, each moved on along its last step
    // for `leadTicks` (how many ticks the input is ahead of the creatures the server sent last).
    [[nodiscard]] QcVec3 pushOnPlayer(const QcVec3 &feet, const QcContentRegistry &content, int leadTicks) const;

    // Whether the session moves the creatures on at all: not with the environment variable
    // QTCRAFT_PUSH_EXTRAPOLATION=0 (to measure what it saves, test_push_client).
    [[nodiscard]] static bool extrapolationWanted();

private:
    struct Creature
    {
        qint32 type = 0;
        QcVec3 feet;
        // Its horizontal move between the last two places the server sent: one server tick.
        QcVec3 step;
        quint16 flags = 0;
    };

    // Where `creature` stands `leadTicks` ticks after its last place, along its last step.
    [[nodiscard]] static QcVec3 ahead(const Creature &creature, int leadTicks);

    QHash<quint32, Creature> m_creatures;
};
