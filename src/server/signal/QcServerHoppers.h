#pragma once

#include "QcBlockDefinition.h"
#include "QcBlockPos.h"
#include "QcInventoryAccess.h"

#include <QHash>
#include <QList>
#include <QSet>

#include <vector>

class QcSignalTable;
struct QcServerState;

// The Signal Hoppers of the server (decision E-344), run at the end of the signal phase. Only awake
// hoppers whose cooldown has ended and that no power locks act, in two simultaneous steps on the
// world as it is: first every hopper pushes one item from its first slot that fits into the
// container it faces, then every hopper pulls one item from the container above it or, when there
// is none and the block above does not fill its cell with collision, one item entity (as much of
// its stack as fits) lying in its bowl or the space above. A hopper that moved something rests
// for the cooldown; one that found nothing sleeps until a block next to it changes, a container next
// to it changes its contents or an item moves into its pickup area.
class QcServerHoppers
{
public:
    // The pickup area starts at the bottom of the bowl (in the hopper's cell) and covers the cell above.
    static constexpr double bowlHeight = 10.0 / 16.0;
    // A hopper next to an unloaded chunk looks again after this many ticks.
    static constexpr int unloadedRetryTicks = 20;

    struct Stats
    {
        // Hoppers woken, acting, searching item entities and moving something in the latest tick.
        qsizetype woken = 0;
        qsizetype acting = 0;
        qsizetype searches = 0;
        qsizetype moved = 0;
    };

    QcServerHoppers(QcServerState &state, const QcSignalTable &table);

    void tick();
    const Stats &lastTick() const { return m_stats; }
    // Whether the collision of block state `block` fills its whole cell (then no item lies in the
    // space above a hopper under it, and it does not search).
    bool fillsCell(QcBlockId block) const;

private:
    struct Pull
    {
        QcBlockPos hopper;
        // A container above (slot >= 0) or an item entity (entity != 0).
        QcBlockPos source;
        int slot = -1;
        quint32 entity = 0;
        QcItemStack stack;
        // Where it goes in the hopper: slot and count, decided on the world before the step.
        QList<std::pair<int, int>> places;
    };

    // Wakes the hoppers around changed cells and below moving items.
    void wakeAround();
    // The awake hoppers that act now, at most QcServerLimits::hopperBudget; the rest waits for the
    // next tick, taken from where this one stopped (only then is a step not simultaneous for all).
    QList<QcBlockPos> actors();
    void push(const QList<QcBlockPos> &hoppers, QcInventoryAccess &access, QSet<QcBlockPos> &moved);
    void pull(const QList<QcBlockPos> &hoppers, QcInventoryAccess &access, QSet<QcBlockPos> &moved);
    // The pull from a container above; a hopper that has to look for item entities is added to
    // `searching` instead.
    std::optional<Pull> pullFor(QcBlockPos hopper, QcInventoryAccess &access, QList<QcBlockPos> &searching) const;
    std::optional<Pull> pickUp(QcBlockPos hopper, QcInventoryAccess &access);
    // Carries out a pull; false when nothing moved.
    bool apply(const Pull &pull, QcInventoryAccess &access);
    // Awake item entities by cell, only for the cells the searching hoppers look at.
    void buildGrid(const QList<QcBlockPos> &searching);
    std::optional<QcBlockFace> facingOf(QcBlockPos hopper) const;

    QcServerState &m_state;
    const QcSignalTable &m_table;
    std::vector<bool> m_fillsCell;
    QHash<QcBlockPos, QList<qsizetype>> m_grid;
    std::optional<QcBlockPos> m_cursor;
    Stats m_stats;
};
