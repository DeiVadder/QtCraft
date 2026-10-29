// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/chunks/QcChunkGroups.h"

#include "server/chunks/QcChunkLinks.h"

#include <QSet>

namespace QcChunkGroups {

Group of(QcChunkPos start, const MarksFunction &marksOf, int limit)
{
    Group group;
    const std::optional<quint16> first = marksOf(start);
    group.chunks.append(start);
    if (!first || *first == 0) {
        return group;
    }

    QSet<QcChunkPos> seen{start};
    for (qsizetype next = 0; next < group.chunks.size(); ++next) {
        const QcChunkPos chunk = group.chunks[next];
        const quint16 marks = marksOf(chunk).value_or(0);
        for (int side = 0; side < QcChunkLinks::sideCount; ++side) {
            const QcChunkPos neighbour = QcChunkLinks::neighbour(chunk, side);
            if (seen.contains(neighbour)) {
                continue;
            }
            const std::optional<quint16> other = marksOf(neighbour);
            if (!other || !QcChunkLinks::linked(marks, *other, side)) {
                continue;
            }
            seen.insert(neighbour);
            if (group.chunks.size() >= limit) {
                group.oversize = true;
                return group;
            }
            group.chunks.append(neighbour);
        }
    }
    return group;
}

} // namespace QcChunkGroups
