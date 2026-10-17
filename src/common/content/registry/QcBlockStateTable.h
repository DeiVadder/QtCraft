#pragma once

#include "common/content/definitions/QcBlockDefinition.h"
#include "common/content/registry/QcBlockState.h"

#include <QList>

#include <optional>

// Runtime ids are block states: every block owns a contiguous range of ids, one per combination of
// its property values (the first property varies fastest). The table holds the derived data of
// every state; it is rebuilt by the registry whenever definitions or shapes change.
class QcBlockStateTable
{
public:
    // Signal Dust needs 16 strengths times 81 connection shapes (decision E-334).
    static constexpr int maxStatesPerBlock = 2048;
    static constexpr qsizetype maxStates = qsizetype(1) << 16;
    // States of a block that turn, flip and select the same parts share their derived data; this
    // limits the boxes of all distinct variants of all blocks together (memory and build time).
    static constexpr qsizetype maxDerivedBoxes = qsizetype(1) << 17;

    // Product of the value counts; 0 when a property has no values.
    static qsizetype stateCountOf(const QcBlockDefinition &block);
    // Why the definition cannot be used (too many properties, values or states), or empty.
    static QString problem(const QcBlockDefinition &block);

    // Boxes the distinct variants of a block's states derive from its shape.
    static qsizetype derivedBoxes(const QcBlockDefinition &block, const QcShapeDefinition &shape);
    // Total of derivedBoxes() over all blocks in the table.
    qsizetype totalDerivedBoxes() const { return m_totalBoxes; }
    // Whether the table stays within maxDerivedBoxes when block `type` (or a new block for
    // std::nullopt) derives `boxes`.
    bool fits(std::optional<quint32> type, qsizetype boxes) const;

    void clear();
    // Adds the states of the next block (type index = number of blocks added so far). Callers check
    // fits() first.
    void append(const QcBlockDefinition &block, const QcShapeDefinition &shape);
    // Recomputes the states of an existing block whose state count did not change.
    void update(quint32 type, const QcBlockDefinition &block, const QcShapeDefinition &shape);

    qsizetype size() const { return m_states.size(); }
    qsizetype typeCount() const { return m_firstState.size(); }
    // Precondition: id < size().
    const QcBlockState &state(quint32 id) const { return m_states[qsizetype(id)]; }
    quint32 firstState(quint32 type) const { return m_firstState[qsizetype(type)]; }
    int stateCount(quint32 type) const { return m_stateCounts[qsizetype(type)]; }

    // Value of `property` in state `index` of `block`, empty when the block has no such property.
    static QString value(const QcBlockDefinition &block, quint32 index, const QString &property);
    // State index with `property` set to `value`; std::nullopt for unknown properties or values.
    [[nodiscard]] static std::optional<quint32> withValue(const QcBlockDefinition &block, quint32 index, const QString &property,
                                            const QString &value);
    // "[a=x,b=y]" for blocks with properties, else empty.
    static QString describe(const QcBlockDefinition &block, quint32 index);

    // Derived data of one state; exposed for tests.
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
