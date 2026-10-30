// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"

struct QcJsonFieldReader;

/*! \brief Parses the explosion fields of a block (decision E-600): "blastResistance" and
    "explosive". */
namespace QcBlastParser {

/*! Returns the explosion fields read by \a reader, which also takes the failures; \a block (read
    so far) decides whether the block may be explosive. */
[[nodiscard]] QcBlastDefinition parseBlast(const QcBlockDefinition &block, QcJsonFieldReader &reader);

} // namespace QcBlastParser
