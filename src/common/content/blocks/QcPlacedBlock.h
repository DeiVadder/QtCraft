// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"
#include "common/core/QcBlockPos.h"

/*! \brief A block state at a cell: one block a plan, a rule or a server system places or changes
    (decision E-975). Protocol messages keep their own types. */
struct QcPlacedBlock
{
    /*! The cell in world coordinates. */
    QcBlockPos pos;
    /*! The block state placed there. */
    QcBlockId block = qcAirBlockId;

    bool operator==(const QcPlacedBlock &) const = default;
};

Q_DECLARE_TYPEINFO(QcPlacedBlock, Q_PRIMITIVE_TYPE);
