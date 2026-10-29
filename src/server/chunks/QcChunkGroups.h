// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcChunkPos.h"

#include <QList>

#include <functional>
#include <optional>

// Groups of chunks that circuits or machines connect (decision E-375): the chunks reached from one
// over links (QcChunkLinks::linked) load and unload as a whole, at a tick boundary, so a machine
// never runs with a part missing. A group larger than the limit is not kept whole: its chunks come
// and go one by one and its circuits wait at the edge of the loaded area (decision E-348).
namespace QcChunkGroups {

struct Group
{
    QList<QcChunkPos> chunks;
    // More chunks than the limit are linked; `chunks` holds the first ones found.
    bool oversize = false;
};

// The marks of a chunk (QcChunkLinks) or none when it does not count (not loaded, unknown).
using MarksFunction = std::function<std::optional<quint16>(QcChunkPos)>;

// The group of `start` over the chunks `marksOf` knows, at most `limit` chunks (breadth first, the
// sides in a fixed order, so the same world gives the same group).
Group of(QcChunkPos start, const MarksFunction &marksOf, int limit);

} // namespace QcChunkGroups
