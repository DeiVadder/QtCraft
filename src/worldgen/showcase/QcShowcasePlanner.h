#pragma once

#include "worldgen/showcase/QcShowcaseArea.h"
#include "worldgen/showcase/QcShowcasePlan.h"

#include <QStringList>

// Lays out the showcase world from the content (decision E-220), the same for every seed: north of
// the spawn the demos from structure data, the trees, the creatures, a podium area per block
// category and the items, in rows from west to east; each area gets a title and a lookout in front
// of its south west corner, which is also a tour stop. Blocks, creatures and items of mods appear
// without any extra data.
class QcShowcasePlanner
{
public:
    // Distance between areas in a row and between rows.
    static constexpr int areaGap = 6;
    static constexpr int rowGap = 7;
    // West edge of the first area and south edge of the first row.
    static constexpr int firstAreaX = 6;
    static constexpr int firstRowFront = -6;

    explicit QcShowcasePlanner(const QcContentRegistry &content);

    QcShowcasePlan build();
    // What the planner left out (unknown structures, labels beyond a chunk's limit).
    const QStringList &warnings() const { return m_warnings; }

private:
    struct Cursor
    {
        int x = firstAreaX;
        int front = firstRowFront;
        // Northmost z of the areas in the current row.
        int north = firstRowFront;
    };

    QcShowcaseContext context() const;
    QList<QcShowcaseAreaBuilder> builders(const QcShowcaseContext &context);
    QList<QcShowcaseAreaBuilder> demoBuilders();
    // An area built and the cursor after it.
    struct Placed
    {
        QcShowcaseArea area;
        Cursor next;
    };
    // Builds the area at the cursor, in a new row when the current one is full.
    [[nodiscard]] Placed placeNext(const QcShowcaseAreaBuilder &builder, Cursor cursor) const;
    void addArea(const QcShowcaseArea &area, int front);
    void addPlacement(QcStructurePlacement placement);
    void addLookout(const QcShowcaseArea &area, int front);
    void addLabel(const QcWorldLabel &label);
    void warn(const QString &warning) { m_warnings.append(warning); }

    const QcContentRegistry &m_content;
    QStringList m_warnings;
    QList<QcBlockId> m_groundColumn;
    std::shared_ptr<const QcStructure> m_lookout;
    QcShowcasePlan m_plan;
};
