#pragma once

#include "worldgen/villages/QcVillageDraft.h"

#include <QList>

#include <optional>
#include <utility>

class QcContentRegistry;
class QcTerrainGenerator;

// Lays one road of a village (decision E-408, E-419): its runs in eight directions, bending where
// its street plan says and winding with the terrain, cut short at water, steep steps, pieces and the
// village's radius, recorded cell by cell in the draft; over water the boardwalk's stairs down to
// land at its end. Deterministic and thread-safe (const).
class QcRoadLayer
{
public:
    using Road = QcVillageDraft::Road;

    // A road to lay: from (x, z) `length` cells in `direction` (an index into
    // QcVillageDraft::directions), turning by `bends` (distance along it, eighths of a turn; positive
    // clockwise), winding with the terrain, cut short at water, steep steps, pieces and the radius.
    struct Arm
    {
        int x = 0;
        int z = 0;
        int direction = 0;
        int length = 0;
        int firstSlot = 0;
        bool branch = false;
        QList<std::pair<int, int>> bends;
        bool winding = false;
        // Laid only in full, without a road end (a ring back to its first cell, a plaza's spoke).
        bool closed = false;
        int sides = 3;
    };

    QcRoadLayer(const QcTerrainGenerator &terrain, const QcContentRegistry &content);

    // Lays an arm: its runs, recorded with their cells; empty when nothing past its first slot is
    // left (or a closed arm is cut).
    QList<Road> layArm(QcVillageDraft &draft, const Arm &arm) const;
    // The runs layArm() would lay, without recording them.
    QList<Road> walkArm(QcVillageDraft &draft, const Arm &arm) const;

private:
    // A road ends where its ground steps up or down by more than this.
    static constexpr int roadStep = 2;

    // The ground height of the next cell of a road, coming from ground of height `previous`;
    // std::nullopt where the road ends there (water, a steep step, a piece, the radius).
    [[nodiscard]] std::optional<int> roadGround(const QcVillageDraft &draft, std::pair<int, int> cell, int direction, int previous,
                                                bool checkPieces) const;
    // The turn (-1, 0, 1 eighths) of a winding road towards the flatter neighbouring direction, at
    // most one eighth off its `heading`.
    int windTurn(QcVillageDraft &draft, std::pair<int, int> cell, int direction, int heading) const;
    // The cells of laid runs outside the pieces: ground cells on land, boardwalk decks over water;
    // the first rank laid on a cell keeps it.
    void recordRuns(QcVillageDraft &draft, const QList<Road> &runs, bool closed) const;
    // The cells of one run; `joins` (previous, next) squares its first or last cell's brush.
    void paintRun(QcVillageDraft &draft, const Road &run, std::pair<bool, bool> joins) const;

    const QcTerrainGenerator &m_terrain;
    const QcContentRegistry &m_content;
};
