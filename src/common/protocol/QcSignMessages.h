#pragma once

// Protocol messages about signs (decision E-766). Part of the QcMessage variant (QcMessages.h).

#include "common/core/QcBlockPos.h"
#include "common/world/QcSignText.h"

namespace QcMsg {

// Server -> client: the text on the sign at `pos`, sent after the ChunkData of its chunk and
// whenever it changes; all lines empty: no text (cleared, or the sign is gone). `color` is the
// colour a dye gave the text (QcSignText::Text::color, decision E-830; empty for the default).
struct SignText
{
    QcBlockPos pos;
    QcSignText::Lines lines;
    QString color;
};

// Server -> client: the player placed or used the sign at `pos`; the client opens its editor with
// the sign's current lines.
struct OpenSignEditor
{
    QcBlockPos pos;
    QcSignText::Lines lines;
};

// Client -> server: the lines the player wrote on the sign at `pos`. The server checks the sign,
// the reach and the rate and cleans the lines (QcSignText::cleaned) before it keeps them.
struct EditSign
{
    QcBlockPos pos;
    QcSignText::Lines lines;
};

} // namespace QcMsg
