// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/physics/QcSpawnGround.h"

namespace QcSpawnGround {

bool carries(const QcContentRegistry &content, QcBlockId ground)
{
    const QcBlockDefinition &block = content.block(ground);
    return block.solid && block.renderLayer == QcRenderLayer::Opaque && !block.keepsSpawnsAway()
           && content.state(ground).fullFace[static_cast<std::size_t>(QcBlockFace::Up)];
}

bool leavesRoom(const QcContentRegistry &content, QcBlockId cell)
{
    const QcBlockDefinition &block = content.block(cell);
    return !block.solid && !block.liquid && !block.rail && !block.keepsSpawnsAway();
}

Carrying carryingOf(const QcContentRegistry &content, quint32 type)
{
    const QcBlockStateTable &states = content.stateTable();
    const QcBlockId first = states.firstState(type);
    int carrying = 0;
    for (int index = 0; index < states.stateCount(type); ++index) {
        carrying += carries(content, static_cast<QcBlockId>(first + static_cast<quint32>(index)));
    }

    if (carrying == 0) {
        return Carrying::None;
    }
    return carrying == states.stateCount(type) ? Carrying::All : Carrying::Some;
}

} // namespace QcSpawnGround
