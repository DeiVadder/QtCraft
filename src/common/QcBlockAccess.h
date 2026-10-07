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
    virtual std::optional<QcBlockId> blockAt(QcBlockPos pos) const = 0;
    virtual const QcContentRegistry &content() const = 0;
};
