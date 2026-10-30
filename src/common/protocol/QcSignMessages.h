// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Protocol messages about signs (decision E-766). Part of the QcMessage variant (QcMessages.h).

#include "common/core/QcBlockPos.h"
#include "common/world/QcSignText.h"

/* QcMsg is documented in QcSessionMessages.h. */
namespace QcMsg {

/*! \brief Server -> client: the text on the sign at \c {pos}, sent after the ChunkData of its chunk
    and whenever it changes; all lines empty: no text (cleared, or the sign is gone).

    \c {color} is the colour a dye gave the text (QcSignText::Text::color, decision E-830; empty
    for the default). */
struct SignText
{
    /*! The sign's block. */
    QcBlockPos pos;
    /*! The lines. */
    QcSignText::Lines lines;
    /*! The text's colour (see the struct). */
    QString color;
};

/*! \brief Server -> client: the player placed or used the sign at \c {pos}; the client opens its
    editor with the sign's current lines. */
struct OpenSignEditor
{
    /*! The sign's block. */
    QcBlockPos pos;
    /*! The sign's current lines. */
    QcSignText::Lines lines;
};

/*! \brief Client -> server: the lines the player wrote on the sign at \c {pos}.

    The server checks the sign, the reach and the rate and cleans the lines (QcSignText::cleaned)
    before it keeps them. */
struct EditSign
{
    /*! The sign's block. */
    QcBlockPos pos;
    /*! The lines as written. */
    QcSignText::Lines lines;
};

} // namespace QcMsg
