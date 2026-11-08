// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/recipes/QcSmithingRecipe.h"
#include "common/core/QcResult.h"

#include <QDataStream>
#include <QList>

class QcContentRegistry;

/*! \brief The smithing recipes in the content sync (QcContentSerializer, decision E-1115), so the
    client's handbook and smithing screen know them. */
namespace QcSmithingSerializer {

/*! Writes the count of \a recipes and then each of them to \a stream. */
void write(QDataStream &stream, const QList<QcSmithingRecipe> &recipes);
/*! Reads the recipes written by write() from \a stream and registers each in \a registry;
    returns an error at the first corrupt one (an empty name). */
[[nodiscard]] QcStatus read(QDataStream &stream, QcContentRegistry &registry);

} // namespace QcSmithingSerializer
