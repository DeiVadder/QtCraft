#pragma once

#include "common/maps/QcMapData.h"

#include <QList>
#include <QPointF>

#include <optional>

class QcServerWorld;

// How a map is revealed (decision E-800), as in the reference: a player holding a map looks at
// the pixels within revealRadius blocks of where it stands, one column of pixels in `sweep` per
// tick, so a map fills in over a second while its holder walks and each tick costs little. A pixel
// shows the most common top block of the columns it covers (one column at scale 0; blocks drawn
// nowhere, like the barrier, are looked through): land is shaded by the mean height of its columns
// against the pixel north of it, water by its mean depth, both with a fine checker pattern; pixels
// with a column in a chunk that is not loaded stay as they are.
namespace QcMapSurvey {

inline constexpr int revealRadius = 128;
inline constexpr int sweep = 16;
// Water this deep or deeper counts as deep (no checker pattern any more).
inline constexpr int deepWater = 10;

// The pixels of `map` that a holder standing at `holder` (block x, z) looks at in tick `tick`.
[[nodiscard]] QList<QPoint> pixelsDue(const QcMapData &map, QPointF holder, quint64 tick);
// What pixel `pixel` of `map` shows now (QcMapPixel); std::nullopt when a chunk it covers is not
// loaded.
[[nodiscard]] std::optional<quint16> pixelOf(const QcServerWorld &world, const QcMapData &map, QPoint pixel);

// One pixel looked at: where it is and what it shows now.
struct Surveyed
{
    QPoint pixel;
    quint16 value = 0;
};
// What `pixels` (in the order of pixelsDue) show now, leaving out those over unloaded chunks; the
// columns of a pixel are counted once, also as the north neighbour of the pixel after it.
[[nodiscard]] QList<Surveyed> survey(const QcServerWorld &world, const QcMapData &map, const QList<QPoint> &pixels);

} // namespace QcMapSurvey
