// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/content/worldgen/QcDimensionDefinition.h"
#include "common/core/QcVec3.h"
#include "common/world/QcBlockAccess.h"
#include "server/core/QcServerLimits.h"

#include <QHash>
#include <QList>

#include <functional>
#include <map>
#include <memory>
#include <optional>

/*! \brief Flowing liquids on the server (decision E-054).

    A liquid block with a \c {flow} definition takes a flow step \c {ticks} server ticks after
    something next to it changed: a flowing part (level > 0) takes the level of its best feeder
    plus one, falls back to nothing without one, and becomes a source between two sources over
    solid ground when the liquid is infinite; then the liquid falls into the cell below if it can
    and otherwise (and always for sources) spreads sideways one level further, up to its
    distance, preferring the sides that lead to the nearest drop. Liquids touching another liquid
    they react with turn into the reaction's block. Steps are planned per position (each at most
    once) and worked off in order of their due tick, at most a budget per tick; everything a step
    changes plans its neighbours. */
class QcLiquidFlow
{
public:
    /*! Flow steps per server tick at most by default (QcServerLimits::liquidStepsPerTick); the
        rest waits for the next tick. */
    static constexpr int maxStepsPerTick = static_cast<int>(QcServerLimits{}.liquidStepsPerTick);
    /*! Planned steps at most by default (QcServerLimits::liquidPlannedSteps); further plans are
        dropped (the cell flows again when touched). */
    static constexpr qsizetype maxPlanned = QcServerLimits{}.liquidPlannedSteps;

    /*! Replaces the block at a position (the caller breaks washable blocks first and tells
        clients). */
    using SetFunction = std::function<void(QcBlockPos pos, QcBlockId block)>;

    /*! \brief How far a liquid runs. \qcfields */
    struct Reach
    {
        /*! Blocks it runs sideways from its source. */
        int distance = 0;
        /*! Blocks it looks sideways for the nearest drop. */
        int dropSearch = 0;
    };

    /*! \brief What the steps of one tick did. */
    struct TickResult
    {
        /*! Flow steps taken. */
        int steps = 0;
        /*! Blocks the steps changed. */
        int changes = 0;
        /*! Positions whose chunk was not loaded when their step was due. */
        QList<QcBlockPos> unloaded;
    };

    explicit QcLiquidFlow(std::shared_ptr<const QcContentRegistry> content, const QList<QcDimensionDefinition::LiquidFlow> &flows = {});

    /*! Plans a step for the liquid at \a pos of \a world after its flow ticks, counted from
        \a now. Returns \c false for blocks that do not flow, positions already planned and when
        the plan is full. */
    bool schedule(const QcBlockAccess &world, QcBlockPos pos, quint64 now);
    /*! Runs the steps due at \a now in \a world, at most \a budget, setting blocks with
        \a set. Returns what they did. */
    TickResult tick(quint64 now, const QcBlockAccess &world, const SetFunction &set, int budget = maxStepsPerTick);

    /*! Returns whether a planned step is due at \a now. */
    bool hasDueSteps(quint64 now) const { return !m_due.empty() && m_due.begin()->first <= now; }
    /*! Returns the number of planned steps. */
    qsizetype plannedCount() const { return m_planned.size(); }
    /*! Sets the planned steps at most to \a limit. */
    void setMaxPlanned(qsizetype limit) { m_maxPlanned = limit; }
    /*! Returns the positions with a planned step. */
    QList<QcBlockPos> plannedPositions() const { return m_planned.keys(); }
    /*! Returns the plans dropped because the plan was full. */
    quint64 droppedCount() const { return m_dropped; }
    /*! Drops every planned step. */
    void clear();
    /*! Drops the planned steps inside \a chunk (it was generated again). */
    void forgetChunk(QcChunkPos chunk);
    /*! Returns the planned steps inside \a chunks by chunk and takes them out (they leave the
        world, decision E-374); they come back as block updates, which plan them again. */
    QHash<QcChunkPos, QList<QcBlockPos>> takeChunks(const QSet<QcChunkPos> &chunks);

    /*! Returns whether the block \a id of \a content is a liquid that flows (rules, shared with
        tests). */
    static bool flows(const QcContentRegistry &content, QcBlockId id);
    /*! Returns the \c {level} property of the liquid \a id of \a content (0 for a source). */
    static int level(const QcContentRegistry &content, QcBlockId id);
    /*! Returns whether the liquid \a id of \a content is falling. */
    static bool isFalling(const QcContentRegistry &content, QcBlockId id);
    /*! Returns whether the liquid \a id of \a content is a source. */
    static bool isSource(const QcContentRegistry &content, QcBlockId id);
    /*! Returns the horizontal direction (unit length, or zero) the liquid at \a pos of
        \a world pushes floating things:
        down the slope of its surface towards shallower parts of the same liquid and open cells,
        like the reference game (decision E-296). Zero for still liquid and cells that hold no
        liquid. */
    static QcVec3 current(const QcBlockAccess &world, QcBlockPos pos);
    /*! Returns the state of the liquid of \a id in \a content with \a level and the flag
        \a falling. */
    static QcBlockId stateWith(const QcContentRegistry &content, QcBlockId id, int level, bool falling);
    /*! Returns what the liquid \a id at \a pos of \a world turns into because it touches a
        liquid it reacts with. */
    [[nodiscard]] static std::optional<QcBlockId> reaction(const QcBlockAccess &world, QcBlockPos pos, QcBlockId id);
    /*! Returns the state the flowing (non-source) liquid \a id at \a pos of \a world takes from
        its feeders when it reaches \a distance blocks, air without any; std::nullopt while a
        neighbour that decides it is not loaded. */
    [[nodiscard]] static std::optional<QcBlockId> fedState(const QcBlockAccess &world, QcBlockPos pos, QcBlockId id, int distance);
    /*! Returns whether the liquid may flow into \a pos of \a world as \a state (air, replaceable
        or washable blocks, or a weaker part of the same liquid). */
    static bool canFlowInto(const QcBlockAccess &world, QcBlockPos pos, QcBlockId state);
    /*! Returns whether the liquid may flow as \a state into a cell holding \a block inside the
        world height, by the blocks of \a content (std::nullopt: not loaded; the caller checks the
        world height, as canFlowInto() does). */
    static bool canReplace(const QcContentRegistry &content, std::optional<QcBlockId> block, QcBlockId state);
    /*! Returns the sides the liquid at \a pos of \a world spreads to as \a spread: of the sides
        it can flow into, those whose way leads to the nearest drop within the liquid's
        \c {dropSearch} blocks and the reach left to \a spread of the \a reach blocks it
        runs (decision E-103), or all of them when no drop is that near. */
    static QList<QcBlockFace> spreadFaces(const QcBlockAccess &world, QcBlockPos pos, QcBlockId spread, Reach reach);

private:
    // What the steps of one tick work on.
    struct StepContext
    {
        quint64 now = 0;
        const QcBlockAccess &world;
        const SetFunction &set;
        TickResult &result;
    };

    // How a liquid flows here: its own values or those of the dimension.
    struct Flow
    {
        Reach reach;
        int ticks = 0;
    };

    [[nodiscard]] Flow flowOf(QcBlockId id) const;
    void step(QcBlockPos pos, const StepContext &context);
    void change(QcBlockPos pos, QcBlockId block, const StepContext &context);

    std::shared_ptr<const QcContentRegistry> m_content;
    // By block type, the liquids that flow differently in this dimension.
    QHash<quint32, Flow> m_flows;
    // Due tick -> positions in the order they were planned.
    std::map<quint64, QList<QcBlockPos>> m_due;
    QHash<QcBlockPos, quint64> m_planned;
    qsizetype m_maxPlanned = maxPlanned;
    quint64 m_dropped = 0;
};
