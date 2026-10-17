#pragma once

#include "common/core/QcAabb.h"
#include "common/core/QcVec3.h"
#include "server/signal/QcPistonPlanner.h"

#include <QHash>

#include <array>

class QcServerBlocks;
class QcServerSounds;
class QcSignalTable;
class QcServerState;

// Carries out piston plans (decision E-337). Blocks move in the world at once, nearest last, each
// with its chest or furnace contents: the contents are re-keyed to the new cell before the block is
// set there, so nothing is dropped, copied or lost, also across chunk borders and in a save made in
// the middle of a move. Bodies in the cells the blocks and the head move into (every body of the
// entity register QcEntities: players, creatures, items, falling blocks, armour stands) are pushed
// out along the piston's direction by the server; players get their new position like any other
// server correction. The clients see the blocks slide (QcMovingBlocks).
class QcPistonMover
{
public:
    // Room left between a pushed body and the block that pushed it.
    static constexpr double pushGap = 0.01;

    QcPistonMover(QcServerState &state, QcServerBlocks &blocks, QcServerSounds &sounds, const QcSignalTable &table);

    // Returns the work done: blocks moved or broken and bodies looked at.
    qsizetype apply(const QcPistonPlan &plan);
    // Moves the bodies the step's plans pushed: per body the pushes of all plans together (so the
    // order of the plans does not matter), against what is solid after the moves; a body left
    // inside a block goes to the nearest free cell (decision E-337).
    void applyPushes();

    // How far a body (world box) must move along `towards` to leave the cells; 0 when it touches none.
    static double pushDistance(const QcAabb &body, const QList<QcBlockPos> &cells, QcBlockFace towards);

private:
    void extend(const QcPistonPlan &plan);
    void retract(const QcPistonPlan &plan);
    // Moves the block at `from` with its contents to `to`; the clients get it after the slide.
    void moveBlock(QcBlockPos from, QcBlockPos to, QcBlockId block);
    // Notes how far the bodies in `cells` are pushed along `towards`.
    void pushBodies(const QList<QcBlockPos> &cells, QcBlockFace towards);
    void notePush(quint32 body, const QcAabb &box, const QList<QcBlockPos> &cells, QcBlockFace towards);
    // The noted push of a body, removed.
    QcVec3 takePush(quint32 body);
    // How far a body moves for a push: clipped by solid blocks, then out of any block it is in.
    QcVec3 moveBody(const QcAabb &box, const QcVec3 &push) const;
    bool overlapsSolid(const QcAabb &box) const;
    // Where a body stuck in a block may go, in the order tried.
    static QList<QcVec3> escapes(const QcAabb &box, const QcVec3 &push);
    QcBlockId headState(const QcPistonPlan &plan) const;

    QcServerState &m_state;
    QcServerBlocks &m_blocks;
    QcServerSounds &m_sounds;
    const QcSignalTable &m_table;
    // Pushes of the current step per body (entity id) and direction (QcBlockFace).
    QHash<quint32, std::array<double, qcBlockFaceCount>> m_pushes;
    qsizetype m_bodiesLookedAt = 0;
};
