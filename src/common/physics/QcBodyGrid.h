#pragma once

#include "common/physics/QcBodyPush.h"

#include <QtGlobal>

#include <vector>

// Bodies sorted into columns of one block (decision E-882), so that the push on one body
// (QcBodyPush::between) looks only at the bodies in the columns within reach of it instead of at all
// of them: building costs a sort of the bodies, a look-up a binary search per column around the
// body. Keeps its storage when cleared and sorts in place, so a grid built anew every tick allocates
// nothing once it held as many bodies.
class QcBodyGrid
{
public:
    // Forgets all bodies.
    void clear();
    void add(const QcBodyPush::Body &body);
    // Sorts the bodies into their columns: after the last add(), before sum().
    void finish();
    qsizetype size() const { return static_cast<qsizetype>(m_entries.size()); }

    // The sum of the pushes of the bodies on `self` (QcBodyPush::between); `self` itself must not be
    // in the grid.
    [[nodiscard]] QcVec3 sum(const QcBodyPush::Body &self) const;

private:
    struct Entry
    {
        qint64 column = 0;
        // Its place among the bodies added since the last clear().
        qsizetype order = 0;
        QcBodyPush::Body body;
    };

    [[nodiscard]] static qint64 columnOf(int x, int z);

    std::vector<Entry> m_entries;
    // The largest half width of the bodies: how many columns around a body a look-up searches.
    double m_widest = 0.0;
};
