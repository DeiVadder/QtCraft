#pragma once

#include <QString>

// What a damaged per-region file of a world held (decision E-366): the server refuses the world and
// names the file; moved out of the world folder, the world opens without what it held. Journal: the
// last save broke off and cannot be finished; without its journal and staged files the world opens
// as saved before (decision E-378).
enum class QcDamagedPart { None, Blocks, Entities, BlockEntities, Journal };

struct QcDamagedFile
{
    QcDamagedPart part = QcDamagedPart::None;
    // Relative to the world directory.
    QString file;
};
