// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"
#include "common/core/QcBlockPos.h"

#include <optional>

class QcContentRegistry;

/*! \brief Read access to blocks, implemented by the server world and the client world mirror. */
class QcBlockAccess
{
public:
    virtual ~QcBlockAccess() = default;

    /*! Returns the block state at \a pos; std::nullopt when the chunk is not loaded. Positions
        above the world read air. */
    [[nodiscard]] virtual std::optional<QcBlockId> blockAt(QcBlockPos pos) const = 0;
    /*! Returns the content the block ids refer to. */
    virtual const QcContentRegistry &content() const = 0;
    /*! Returns whether a block light source may lie within \a reach blocks of \a pos; worlds
        with chunk palettes answer cheaply, the default always says yes. */
    virtual bool mayHaveLightSourceNear(QcBlockPos pos, int reach) const
    {
        Q_UNUSED(pos);
        Q_UNUSED(reach);
        return true;
    }
};
