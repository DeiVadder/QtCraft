// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcIndexId.h"

#include <QTypeInfo>

/*! \brief Tells creature kinds apart from the ids of other lists (QcIndexId). */
struct QcCreatureKindTag;

/*! The creature kind of a mob: the index of its definition among the content's mobs
    (QcContentRegistry::mobs), as a type of its own (decision E-952). An item, block or entity
    index cannot pass for it. */
using QcCreatureKindId = QcIndexId<QcCreatureKindTag>;

Q_DECLARE_TYPEINFO(QcCreatureKindId, Q_RELOCATABLE_TYPE);
