// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/mobs/QcMobDefinition.h"

#include <QDataStream>

#include <optional>

/*! \brief Wire format of one creature definition in the content sync: what the client shows and
    uses of it (size, model parts, sounds, glow, breeding food, tameable, milking, the thread of a
    family).

    Reading validates everything that later feeds physics and the mesher. */
namespace QcMobSerializer {

/*! Writes the parts of \a mob that the client uses to \a stream. */
void writeMob(QDataStream &stream, const QcMobDefinition &mob);
/*! Returns the creature read from \a stream; std::nullopt (and possibly a corrupt stream) on bad
    data. */
[[nodiscard]] std::optional<QcMobDefinition> readMob(QDataStream &stream);

} // namespace QcMobSerializer
