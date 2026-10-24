#pragma once

#include "worldgen/villages/QcVillageDraft.h"

#include <QList>

#include <optional>
#include <utility>

class QcContentRegistry;
class QcTerrainGenerator;

// Lays one road of a village (decision E-408, E-419): its runs in eight directions, bending where
// its street plan says and winding with the terrain, crossing rivers on bridges (decision E-841), cut
// short at other water, steep steps, pieces and the village's radius, recorded cell by cell in the
// draft; over water the boardwalk's stairs down to land at its end. Deterministic and thread-safe (const).
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
    // A bridge spans at most this many cells of water and low bank.
    static constexpr int maxBridge = 24;

    // Where a road being walked comes from: the ground height of its last cell, how many cells at
    // its end lie on a bridge, and the direction it went.
    struct From
    {
        int height = 0;
        int bridgeCells = 0;
        int direction = 0;
    };
    // The ground of a road's cell: its height, over a river the height above the bridge's deck.
    struct Ground
    {
        int height = 0;
        bool bridge = false;
    };
    // The ground of the next cell of a road, coming `from` its last cell; std::nullopt where the road
    // ends there (water, a steep step, a piece, the radius, a river delta).
    [[nodiscard]] std::optional<Ground> roadGround(const QcVillageDraft &draft, std::pair<int, int> cell, int direction, From from,
                                                   bool checkPieces) const;
    // A run of low bank and water cells from `cell` on in direction (dx, dz): how many (at most one
    // more than maxBridge), whether some is a river's or a delta's (decision E-850), and the height
    // of the dry cell after it.
    struct Stretch
    {
        int cells = 0;
        bool river = false;
        bool delta = false;
        std::optional<int> bankHeight;
    };
    [[nodiscard]] Stretch wetStretch(std::pair<int, int> cell, int dx, int dz) const;
    // Over a river (decision E-841): the height above a bridge's deck when the road at `cell` can
    // cross the water straight on in `direction` to dry land at most a block above or below the
    // deck, at most maxBridge cells on; std::nullopt where it cannot (no bridges in the style, a lake
    // or the sea, too wide, a steep far bank).
    [[nodiscard]] std::optional<int> bridgeHeight(const QcVillageDraft &draft, std::pair<int, int> cell, int direction) const;
    // The height above a bridge's deck, which lies a block above the water.
    [[nodiscard]] int bridgeDeck() const;
    // The runs without their last `cells` cells (runs left empty dropped, the first kept).
    [[nodiscard]] static QList<Road> withoutLast(QList<Road> runs, int cells);
    // The level of a road's middle line at a cell: its top block, over a river the bridge's deck.
    [[nodiscard]] QcVillageDraft::RoadLevel roadLevel(int x, int z) const;
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
