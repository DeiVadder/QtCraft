#pragma once

#include "common/maps/QcMapData.h"

#include <QPoint>
#include <QRect>

#include <array>

// What one player has not received of one map yet (decision E-802): per row the span of pixels
// changed since, and the row to go on from. Sending takes the next changed rows after that row
// (wrapping at the bottom), so rows changing all the time near the top never hold back the rows
// below: every changed row goes out within QcMapData::size takes.
class QcMapPending
{
public:
    // Every pixel waits (a map the player never had).
    [[nodiscard]] static QcMapPending whole();

    void add(QPoint pixel);
    // The next changed rows from the cursor, as one rectangle of at most `budget` pixels (adjacent
    // changed rows, as wide as their widest span); those rows no longer wait. An empty rectangle
    // when nothing waits or one row is wider than the budget.
    [[nodiscard]] QRect take(int budget);
    bool isEmpty() const;

private:
    struct Span
    {
        int left = QcMapData::size;
        int right = -1;

        bool isEmpty() const { return left > right; }
    };

    std::array<Span, QcMapData::size> m_rows{};
    int m_cursor = 0;
};
