// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

/*! \brief What a damaged per-region file of a world held (decision E-366): the server refuses the
    world and names the file; moved out of the world folder, the world opens without what it held.

    Journal: the last save broke off and cannot be finished; without its journal and staged files
    the world opens as saved before (decision E-378). */
enum class QcDamagedPart {
    /*! No file is damaged. */
    None,
    /*! The blocks of a region's chunks: without the file they are generated anew. */
    Blocks,
    /*! The entities of a region (creatures, items, armour stands). */
    Entities,
    /*! The block entities of a region (contents of chests and machines). */
    BlockEntities,
    /*! The journal of a save that broke off. */
    Journal
};

/*! \brief A damaged file of a world and what it held. */
struct QcDamagedFile
{
    /*! What the file held. */
    QcDamagedPart part = QcDamagedPart::None;
    /*! Relative to the world directory. */
    QString file;
};
