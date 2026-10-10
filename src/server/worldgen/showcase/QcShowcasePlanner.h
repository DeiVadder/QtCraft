#pragma once

#include "QcShowcaseArea.h"
#include "QcShowcasePlan.h"

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

    QcShowcasePlanner(const QcContentRegistry &content, QStringList *warnings);

    QcShowcasePlan build();

private:
    struct Cursor
    {
        int x = firstAreaX;
        int front = firstRowFront;
        // Northmost z of the areas in the current row.
        int north = firstRowFront;
    };

    QcShowcaseContext context() const;
    QList<QcShowcaseAreaBuilder> builders(const QcShowcaseContext &context) const;
    QList<QcShowcaseAreaBuilder> demoBuilders() const;
    // Builds the area at the cursor, in a new row when the current one is full.
    QcShowcaseArea placeNext(const QcShowcaseAreaBuilder &builder, Cursor &cursor) const;
    void addArea(const QcShowcaseArea &area, int front);
    void addPlacement(QcStructurePlacement placement);
    void addLookout(const QcShowcaseArea &area, int front);
    void addLabel(const QcWorldLabel &label);
    // Into the warnings list when there is one, else into the log.
    void warn(const QString &warning) const;

    const QcContentRegistry &m_content;
    QStringList *m_warnings = nullptr;
    QList<QcBlockId> m_groundColumn;
    std::shared_ptr<const QcStructure> m_lookout;
    QcShowcasePlan m_plan;
};
