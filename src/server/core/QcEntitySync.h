#pragma once

#include "QcMessages.h"

#include <QList>

struct QcEntity;
struct QcServerClient;
class QcServerState;

// Tells each client about the entities near it (decision E-364), the same way for every kind: a
// spawn when one comes into view or its spawn data changed, moves for known ones and a removal when
// one is out of sight, rests or died. What differs per kind is a rule: how far it is seen, whether
// and when its moves are sent, and what its spawn says.
class QcEntitySync
{
public:
    // Players are visible to each other and other entities are sent to clients within this distance.
    static constexpr double entityViewDistance = 64.0;
    // Dropped items are sent to clients within this distance.
    static constexpr double itemViewDistance = 32.0;
    // Item moves smaller than this are not sent (items bobbing on water).
    static constexpr double itemMoveThreshold = 0.02;
    // Minecarts send moves only while they move (decision E-382).
    static constexpr double cartMoveThreshold = 1e-6;

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

    void consider(Pass &pass, const QcEntity &entity) const;
    // What the client sees of the entity now, as a move (also the base of its spawn).
    QcMsg::EntityMove seen(const QcEntity &entity) const;
    QcMsg::EntitySpawn spawnOf(const QcEntity &entity, const QcMsg::EntityMove &now) const;
    void removeUnseen(Pass &pass) const;

    QcServerState &m_state;
};
