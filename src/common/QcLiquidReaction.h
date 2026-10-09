#pragma once

#include <QString>

// What a liquid turns into where it touches another liquid (lava next to water).
struct QcLiquidReaction
{
    // The other liquid's block name.
    QString with;
    // Block that replaces a source of this liquid, and one that replaces a flowing part of it.
    QString source;
    QString flowing;
};
