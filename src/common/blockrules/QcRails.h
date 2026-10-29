// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/definitions/QcBlockDefinition.h"
#include "common/core/QcBlockFace.h"
#include "common/core/QcVec3.h"
#include "common/world/QcBlockAccess.h"

#include <QList>

#include <array>
#include <optional>
#include <vector>

class QcContentRegistry;

// Rails (decision E-382): the track a rail state lays out and the state a rail takes from the rails
// around it. A track runs straight through its cell along `facing` (straight), rises towards
// `facing` by one block (ascending) or bends from `facing` to the side clockwise of it (curve); a
// straight track faces north or east. A rail connects towards every side where a rail lies level
// with it, or one block higher (it rises there) or lower when that rail, shaped by the rails around
// it, reaches this one; the rules depend only on where rails lie, never on the order of the
// updates, so a layout behaves the same in every rotation and mirror image.
namespace QcRails {

enum class Kind : quint8 { Straight, Ascending, Curve };

// One end of a track: the middle of the cell's side `side` (north, east, south or west), at the
// bottom of the cell or, when `raised`, at its top.
struct End
{
    QcBlockFace side = QcBlockFace::North;
    bool raised = false;

    bool operator==(const End &) const = default;
};

struct Track
{
    std::array<End, 2> ends;
};

bool isRail(const QcContentRegistry &content, QcBlockId block);
// The track of a rail state; nothing for other blocks.
[[nodiscard]] std::optional<Track> trackOf(const QcContentRegistry &content, QcBlockId block);
// Where an end of the track in `cell` lies in the world.
QcVec3 endPoint(QcBlockPos cell, End end);
// The state of the rail `block` (any state of it) laid out as `kind` facing `facing`.
QcBlockId stateOf(const QcContentRegistry &content, QcBlockId block, Kind kind, QcBlockFace facing);
// The state the rail `id` at `pos` takes from the rails around it. `lookSide` (the direction the
// placing player looks) decides between the two straight layouts where the neighbours do not
// (none or all four sides); without it the rail keeps its axis. A neighbour in a chunk that is not
// loaded, or a neighbour of a rail one higher or lower beside it, keeps the rail as it is (loading
// the chunk updates it).
QcBlockId shapedState(const QcBlockAccess &world, QcBlockPos pos, QcBlockId id, std::optional<QcBlockFace> lookSide);
// The eight positions one block higher or lower beside `pos` (slopes): the rails there meet a rail
// at `pos`, and their power lines run through it (decision E-384).
std::array<QcBlockPos, 8> slopeNeighbours(QcBlockPos pos);
// The positions besides the six neighbours whose rail may change shape when a rail at `pos` comes or
// goes: the slope neighbours, and every rail that has a rail one block higher or lower beside it
// whose own neighbours include `pos` (whether that rail meets it depends on them). Up to two blocks
// away sideways and two up or down.
inline constexpr std::size_t shapeNeighbourCount = 50;
std::array<QcBlockPos, shapeNeighbourCount> shapeNeighbours(QcBlockPos pos);

// What a rail state does to the carts on it (decision E-384).
struct Behaviour
{
    QcRailAction action = QcRailAction::None;
    bool powered = false;
};

class Table;

// The cell of the rail whose track meets end `end` of the track `track` in `cell` (the same height,
// one higher or one lower beside it), and which of its ends meets it; nothing at the end of a line.
struct Joint
{
    QcBlockPos cell;
    Track track;
    int entry = 0;
};
[[nodiscard]] std::optional<Joint> joined(const QcBlockAccess &world, const Table &rails, QcBlockPos cell, Track track, int end);
// The rails of the same block as the rail at `cell` along its line, at most `reach` each way, in the
// order of the line (from the end of its first track end to that of its second), `cell` among them
// at `index` (the lines of powered and activator rails, decision E-384).
struct Line
{
    QList<QcBlockPos> cells;
    qsizetype index = 0;
};
Line lineOf(const QcBlockAccess &world, const Table &rails, QcBlockPos cell, int reach);

// The track and behaviour of every block state of a content, looked up once (carts ask every tick).
class Table
{
public:
    explicit Table(const QcContentRegistry &content);

    const std::optional<Track> &trackOf(QcBlockId block) const
    {
        static const std::optional<Track> none;
        return block < m_tracks.size() ? m_tracks[block] : none;
    }
    Behaviour behaviourOf(QcBlockId block) const { return block < m_behaviours.size() ? m_behaviours[block] : Behaviour{}; }

private:
    std::vector<std::optional<Track>> m_tracks;
    std::vector<Behaviour> m_behaviours;
};

} // namespace QcRails
