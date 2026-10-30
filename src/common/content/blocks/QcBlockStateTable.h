// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"
#include "common/content/blocks/QcBlockState.h"

#include <QList>

#include <optional>

/*! \brief Runtime ids are block states: every block owns a contiguous range of ids, one per
    combination of its property values (the first property varies fastest).

    The table holds the derived data of every state; it is rebuilt by the registry whenever
    definitions or shapes change. */
class QcBlockStateTable
{
public:
    /*! Most states of one block (Signal Dust needs 16 strengths times 81 connection shapes,
        decision E-334). */
    static constexpr int maxStatesPerBlock = 2048;
    /*! Most states of all blocks together (65536). */
    static constexpr qsizetype maxStates = qsizetype{1} << 16;
    /*! States of a block that turn, flip and select the same parts share their derived data;
        this limits the boxes of all distinct variants of all blocks together (memory and build
        time). */
    static constexpr qsizetype maxDerivedBoxes = qsizetype{1} << 17;

    /*! Returns the number of states of \a block: the product of its properties' value counts; 0
        when a property has no values. */
    static qsizetype stateCountOf(const QcBlockDefinition &block);
    /*! Returns why \a block cannot be used (too many properties, values or states), or an empty
        string. */
    static QString problem(const QcBlockDefinition &block);

    /*! Returns the number of boxes the distinct variants of the states of \a block derive from
        \a shape. */
    static qsizetype derivedBoxes(const QcBlockDefinition &block, const QcShapeDefinition &shape);
    /*! Returns the total of derivedBoxes() over all blocks in the table. */
    qsizetype totalDerivedBoxes() const { return m_totalBoxes; }
    /*! Returns whether the table stays within maxDerivedBoxes when block \a type (or a new block
        for std::nullopt) derives \a boxes. */
    bool fits(std::optional<quint32> type, qsizetype boxes) const;

    /*! Removes all states and blocks. */
    void clear();
    /*! Adds the states of the next block \a block with its resolved \a shape (type index =
        number of blocks added so far). Callers check fits() first. */
    void append(const QcBlockDefinition &block, const QcShapeDefinition &shape);
    /*! Recomputes the states of the existing block \a type from \a block and \a shape; its state
        count must not have changed. */
    void update(quint32 type, const QcBlockDefinition &block, const QcShapeDefinition &shape);

    /*! Returns the number of states of all blocks. */
    qsizetype size() const { return m_states.size(); }
    /*! Returns the number of blocks in the table. */
    qsizetype typeCount() const { return m_firstState.size(); }
    /*! Returns the derived data of state \a id. Precondition: \a id < size(). */
    const QcBlockState &state(quint32 id) const { return m_states[static_cast<qsizetype>(id)]; }
    /*! Returns the runtime id of the first state of block \a type. */
    quint32 firstState(quint32 type) const { return m_firstState[static_cast<qsizetype>(type)]; }
    /*! Returns the number of states of block \a type. */
    int stateCount(quint32 type) const { return m_stateCounts[static_cast<qsizetype>(type)]; }

    /*! Returns the value of \a property in state \a index of \a block, empty when the block has
        no such property. */
    static QString value(const QcBlockDefinition &block, quint32 index, const QString &property);
    /*! Returns the state index of \a block that is state \a index with \a property set to
        \a value; std::nullopt for unknown properties or values. */
    [[nodiscard]] static std::optional<quint32> withValue(const QcBlockDefinition &block, quint32 index, const QString &property,
                                            const QString &value);
    /*! Returns the property values of state \a index of \a block as "[a=x,b=y]", or an empty
        string for blocks without properties. */
    static QString describe(const QcBlockDefinition &block, quint32 index);

    /*! Returns the derived data of state \a index of \a block with the resolved \a shape; exposed
        for tests. */
    static QcBlockState compute(const QcBlockDefinition &block, const QcShapeDefinition &shape, quint32 index);

private:
    // The states of `count` indices, computed once per distinct variant.
    static QList<QcBlockState> computeAll(const QcBlockDefinition &block, const QcShapeDefinition &shape, int count, quint32 type);

    QList<QcBlockState> m_states;
    QList<quint32> m_firstState;
    QList<int> m_stateCounts;
    QList<qsizetype> m_boxes;
    qsizetype m_totalBoxes = 0;
};
