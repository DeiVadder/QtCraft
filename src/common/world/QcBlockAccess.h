#pragma once

#include "QcBlockDefinition.h"
#include "QcBlockPos.h"

#include <optional>

class QcContentRegistry;

// Read access to blocks, implemented by the server world and the client world mirror.
class QcBlockAccess
{
public:
    virtual ~QcBlockAccess() = default;

    // std::nullopt when the chunk is not loaded. Positions above the world read air.
    [[nodiscard]] virtual std::optional<QcBlockId> blockAt(QcBlockPos pos) const = 0;
    virtual const QcContentRegistry &content() const = 0;
    // Whether a block light source may lie within `reach` blocks of `pos`; worlds with chunk
    // palettes answer cheaply, the default always says yes.
    virtual bool mayHaveLightSourceNear(QcBlockPos pos, int reach) const
    {
        Q_UNUSED(pos);
        Q_UNUSED(reach);
        return true;
    }
};
