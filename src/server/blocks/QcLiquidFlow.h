#pragma once

#include "QcBlockAccess.h"
#include "QcContentRegistry.h"

#include <QHash>
#include <QList>

#include <functional>
#include <map>
#include <memory>
#include <optional>

// Flowing liquids on the server (decision E-054). A liquid block with a `flow` definition takes a
// flow step `ticks` server ticks after something next to it changed: a flowing part (level > 0)
// takes the level of its best feeder plus one, falls back to nothing without one, and becomes a
// source between two sources over solid ground when the liquid is infinite; then the liquid falls
// into the cell below if it can and otherwise (and always for sources) spreads sideways one level
// further, up to its distance, preferring the sides that lead to the nearest drop. Liquids touching another liquid they react with turn into the
// reaction's block. Steps are planned per position (each at most once) and worked off in order of
// their due tick, at most a budget per tick; everything a step changes plans its neighbours.
class QcLiquidFlow
{
public:
    // Flow steps per server tick at most; the rest waits for the next tick.
    static constexpr int maxStepsPerTick = 512;
    // Planned steps at most; further plans are dropped (the cell flows again when touched).
    static constexpr qsizetype maxPlanned = 262144;

    // Replaces the block at a position (the caller breaks washable blocks first and tells clients).
    using SetFunction = std::function<void(QcBlockPos pos, QcBlockId block)>;

    struct TickResult
    {
        int steps = 0;
        int changes = 0;
        // Positions whose chunk was not loaded when their step was due.
        QList<QcBlockPos> unloaded;
    };

    explicit QcLiquidFlow(std::shared_ptr<const QcContentRegistry> content);

    // Plans a step for the liquid at `pos` after its flow ticks, counted from `now`. False for
    // blocks that do not flow, positions already planned and when the plan is full.
    bool schedule(const QcBlockAccess &world, QcBlockPos pos, quint64 now);
    // Runs the steps due at `now`, at most `budget`.
    TickResult tick(quint64 now, const QcBlockAccess &world, const SetFunction &set, int budget = maxStepsPerTick);

    // Whether a planned step is due at `now`.
    bool hasDueSteps(quint64 now) const { return !m_due.empty() && m_due.begin()->first <= now; }
    qsizetype plannedCount() const { return m_planned.size(); }
    QList<QcBlockPos> plannedPositions() const { return m_planned.keys(); }
    quint64 droppedCount() const { return m_dropped; }
    void clear();

    // Rules, shared with tests.
    static bool flows(const QcContentRegistry &content, QcBlockId id);
    static int level(const QcContentRegistry &content, QcBlockId id);
    static bool isFalling(const QcContentRegistry &content, QcBlockId id);
    static bool isSource(const QcContentRegistry &content, QcBlockId id);
    // The state of the liquid of `id` with the given level and falling flag.
    static QcBlockId stateWith(const QcContentRegistry &content, QcBlockId id, int level, bool falling);
    // What the liquid at `pos` turns into because it touches a liquid it reacts with.
    static std::optional<QcBlockId> reaction(const QcBlockAccess &world, QcBlockPos pos, QcBlockId id);
    // The state a flowing (non-source) liquid at `pos` takes from its feeders, air without any;
    // std::nullopt while a neighbour that decides it is not loaded.
    static std::optional<QcBlockId> fedState(const QcBlockAccess &world, QcBlockPos pos, QcBlockId id);
    // Whether the liquid may flow into `pos` as `state` (air, replaceable or washable blocks, or a
    // weaker part of the same liquid).
    static bool canFlowInto(const QcBlockAccess &world, QcBlockPos pos, QcBlockId state);
    // The same for a cell holding `block` inside the world height (std::nullopt: not loaded).
    static bool canReplace(const QcContentRegistry &content, std::optional<QcBlockId> block, QcBlockId state);
    // The sides the liquid at `pos` spreads to as `spread`: of the sides it can flow into, those
    // whose way leads to the nearest drop within the liquid's `dropSearch` blocks and the reach
    // left to `spread` (decision E-103), or all of them when no drop is that near.
    static QList<QcBlockFace> spreadFaces(const QcBlockAccess &world, QcBlockPos pos, QcBlockId spread);

private:
    // What the steps of one tick work on.
    struct StepContext
    {
        quint64 now = 0;
        const QcBlockAccess &world;
        const SetFunction &set;
        TickResult &result;
    };

    void step(QcBlockPos pos, const StepContext &context);
    void change(QcBlockPos pos, QcBlockId block, const StepContext &context);

    std::shared_ptr<const QcContentRegistry> m_content;
    // Due tick -> positions in the order they were planned.
    std::map<quint64, QList<QcBlockPos>> m_due;
    QHash<QcBlockPos, quint64> m_planned;
    quint64 m_dropped = 0;
};
