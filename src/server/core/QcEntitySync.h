#pragma once

#include "QcMessages.h"

#include <QSet>

struct QcServerClient;
struct QcServerState;

// Tells each client about the mobs, other players, falling blocks, dropped items and armour stands near it: spawns
// for new ones, moves for known ones and removals for those out of sight or dead.
class QcEntitySync
{
public:
    // Players are visible to each other and mobs are sent to clients within this distance.
    static constexpr double entityViewDistance = 64.0;
    // Dropped items are sent to clients within this distance.
    static constexpr double itemViewDistance = 32.0;
    // Item moves smaller than this are not sent (items bobbing on water).
    static constexpr double itemMoveThreshold = 0.02;

    explicit QcEntitySync(QcServerState &state);

    // Runs once per tick and client.
    void sync(QcServerClient &client);

private:
    struct Pass
    {
        QcServerClient &client;
        QSet<quint32> visible;
        QcMsg::EntityMoves moves;
    };

    void consider(Pass &pass, const QcMsg::EntitySpawn &spawn, bool hurt);
    void considerPlayers(Pass &pass);
    void considerFallingBlocks(Pass &pass);
    // Blocks sliding where a piston moves them (decision E-337).
    void considerMovingBlocks(Pass &pass);
    void considerItems(Pass &pass) const;
    // Armour stands never move: a spawn when they come into view or change, no moves.
    void considerStands(Pass &pass) const;
    void removeUnseen(Pass &pass) const;

    QcServerState &m_state;
};
