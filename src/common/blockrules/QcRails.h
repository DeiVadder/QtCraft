// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"
#include "common/core/QcBlockFace.h"
#include "common/core/QcVec3.h"
#include "common/world/QcBlockAccess.h"

#include <QList>

#include <array>
#include <optional>
#include <vector>

class QcContentRegistry;

/*! \brief Rails (decision E-382): the track a rail state lays out and the state a rail takes from
    the rails around it.

    A track runs straight through its cell along \c {facing} (straight), rises towards
    \c {facing} by one block (ascending) or bends from \c {facing} to the side clockwise of it
    (curve); a straight track faces north or east. A rail connects towards every side where a
    rail lies level with it, or one block higher (it rises there) or lower when that rail, shaped
    by the rails around it, reaches this one; the rules depend only on where rails lie, never on
    the order of the updates, so a layout behaves the same in every rotation and mirror image. */
namespace QcRails {

/*! The layouts of a track (see the namespace). */
enum class Kind : quint8 {
    /*! Straight through the cell along its facing. */
    Straight,
    /*! Rising by one block towards its facing. */
    Ascending,
    /*! Bending from its facing to the side clockwise of it. */
    Curve
};

/*! \brief One end of a track: the middle of the cell's side \c {side} (north, east, south or
    west), at the bottom of the cell or, when \c {raised}, at its top. */
struct End
{
    /*! The side of the cell. */
    QcBlockFace side = QcBlockFace::North;
    /*! Whether the end lies at the top of the cell. */
    bool raised = false;

    bool operator==(const End &) const = default;
};

/*! \brief The track a rail state lays out through its cell. */
struct Track
{
    /*! Its two ends. */
    std::array<End, 2> ends;
};

/*! Returns whether \a block is a valid block state of \a content and a rail. */
bool isRail(const QcContentRegistry &content, QcBlockId block);
/*! Returns the track of the rail state \a block of \a content; nothing for other blocks. */
[[nodiscard]] std::optional<Track> trackOf(const QcContentRegistry &content, QcBlockId block);
/*! Returns where \a end of the track in \a cell lies in the world. */
QcVec3 endPoint(QcBlockPos cell, End end);
/*! Returns the state of the rail \a block of \a content (any state of it) laid out as \a kind
    facing \a facing. */
QcBlockId stateOf(const QcContentRegistry &content, QcBlockId block, Kind kind, QcBlockFace facing);
/*! Returns the state the rail \a id at \a pos takes from the rails around it in \a world.
    \a lookSide (the direction the placing player looks) decides between the two straight layouts
    where the neighbours do not (none or all four sides); without it the rail keeps its axis. A
    neighbour in a chunk that is not loaded, or a neighbour of a rail one higher or lower beside
    it, keeps the rail as it is (loading the chunk updates it). */
QcBlockId shapedState(const QcBlockAccess &world, QcBlockPos pos, QcBlockId id, std::optional<QcBlockFace> lookSide);
/*! Returns the eight positions one block higher or lower beside \a pos (slopes): the rails there
    meet a rail at \a pos, and their power lines run through it (decision E-384). */
std::array<QcBlockPos, 8> slopeNeighbours(QcBlockPos pos);
/*! The number of shapeNeighbours(). */
inline constexpr std::size_t shapeNeighbourCount = 50;
/*! Returns the positions besides the six neighbours whose rail may change shape when a rail at
    \a pos comes or goes: the slope neighbours, and every rail that has a rail one block higher or
    lower beside it whose own neighbours include \a pos (whether that rail meets it depends on
    them). Up to two blocks away sideways and two up or down. */
std::array<QcBlockPos, shapeNeighbourCount> shapeNeighbours(QcBlockPos pos);

/*! \brief What a rail state does to the carts on it (decision E-384). */
struct Behaviour
{
    /*! What the rail does (QcBlockDefinition::railAction). */
    QcRailAction action = QcRailAction::None;
    /*! Whether the state's "powered" property is true. */
    bool powered = false;
};

class Table;

/*! \brief The cell of the rail whose track meets end \c {end} of the track \c {track} in
    \c {cell} (the same height, one higher or one lower beside it), and which of its ends meets
    it; nothing at the end of a line. */
struct Joint
{
    /*! The cell of the rail met. */
    QcBlockPos cell;
    /*! Its track. */
    Track track;
    /*! The index of its end that meets (0 or 1). */
    int entry = 0;
};
/*! Returns the rail in \a world that meets end \a end (0 or 1) of \a track in \a cell, with the
    tracks of \a rails; nothing at the end of a line. */
[[nodiscard]] std::optional<Joint> joined(const QcBlockAccess &world, const Table &rails, QcBlockPos cell, Track track, int end);
/*! \brief The rails of the same block as the rail at \c {cell} along its line, at most
    \c {reach} each way, in the order of the line (from the end of its first track end to that of
    its second), \c {cell} among them at \c {index} (the lines of powered and activator rails,
    decision E-384). */
struct Line
{
    /*! The rails' cells in the order of the line. */
    QList<QcBlockPos> cells;
    /*! Where \c {cell} stands in \c {cells}. */
    qsizetype index = 0;
};
/*! Returns the line of the rail at \a cell in \a world, at most \a reach rails each way, with the
    tracks of \a rails; empty when there is no rail at \a cell. A loop ends where it meets
    itself. */
Line lineOf(const QcBlockAccess &world, const Table &rails, QcBlockPos cell, int reach);

/*! \brief The track and behaviour of every block state of a content, looked up once (carts ask
    every tick). */
class Table
{
public:
    explicit Table(const QcContentRegistry &content);

    /*! Returns the track of the state \a block; nothing for other blocks. */
    const std::optional<Track> &trackOf(QcBlockId block) const
    {
        static const std::optional<Track> none;
        return block < m_tracks.size() ? m_tracks[block] : none;
    }
    /*! Returns the behaviour of the state \a block; none for other blocks. */
    Behaviour behaviourOf(QcBlockId block) const { return block < m_behaviours.size() ? m_behaviours[block] : Behaviour{}; }

private:
    std::vector<std::optional<Track>> m_tracks;
    std::vector<Behaviour> m_behaviours;
};

} // namespace QcRails
