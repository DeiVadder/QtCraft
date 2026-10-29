// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/light/QcEntityLight.h"
#include "clientcore/meshing/QcSignTextMesher.h"
#include "common/core/QcBlockPos.h"
#include "common/core/QcChunkPos.h"
#include "common/world/QcSignText.h"

#include <QHash>
#include <QList>

#include <optional>
#include <vector>

// The text quads of the signs of the loaded chunks (decision E-766), kept from frame to frame: a
// sign's quads are built again only when its text or its colour (decision E-830), its block (state)
// or its light changes, or when
// the atlas they are cut from is replaced. The signs are grouped by chunk, so a frame looks only at
// the chunks within the draw distance instead of at every sign.
class QcSignTextCache
{
public:
    // Takes the signs of the loaded chunks; nothing happens while `generation` is the one of the
    // last call (QcChunkExtras::signGeneration). Built quads of signs whose lines and colour stay
    // are kept.
    void setSigns(const QHash<QcBlockPos, QcSignText::Text> &signs, quint64 generation);
    // Forgets every sign and built quad: the atlas or the world changed.
    void clear();

    // The signs in the chunks within `distance` (metres, horizontally) of `viewer`, and only those
    // whose middle lies within `distance`.
    [[nodiscard]] QList<QcBlockPos> signsNear(const QcVec3 &viewer, double distance) const;
    // The quads of the sign at `pos`, shown by block `block` in `light`; built with `content` and
    // `uv` when the block or the light differ from the last call. Empty for unknown positions and
    // blocks that are no signs. Positions are relative to the corner of the sign's block (decision
    // E-883), so they stay exact however far out the sign stands.
    [[nodiscard]] const std::vector<QcSignTextMesher::Quad> &quads(QcBlockPos pos, QcBlockId block, QcEntityLight light,
                                                                   const QcContentRegistry &content, const QcEntityMesher::UvLookup &uv);

    // How often quads were built since the cache was made (tests and measurements).
    [[nodiscard]] int builds() const { return m_builds; }

private:
    // Light differences below 1/64 do not rebuild the text.
    static constexpr float lightSteps = 64.0f;

    struct Sign
    {
        QcSignText::Text text;
        std::optional<QcBlockId> block;
        // Quantised sky and block light the quads were built with.
        std::pair<int, int> light{-1, -1};
        std::vector<QcSignTextMesher::Quad> quads;
    };

    static std::pair<int, int> lightKey(QcEntityLight light);

    QHash<QcBlockPos, Sign> m_signs;
    QHash<QcChunkPos, QList<QcBlockPos>> m_chunks;
    std::optional<quint64> m_generation;
    int m_builds = 0;
};
