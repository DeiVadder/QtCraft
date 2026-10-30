// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcResult.h"

#include <QDataStream>

class QcAdvancementCatalog;
class QcContentRegistry;

/*! \brief The advancements in the content sync (QcContentSerializer, decision E-923): everything
    the client shows (tree, icon, texts, frame, handbook link) and the names and requirement groups
    of the criteria; the triggers and conditions stay on the server (the client gets Impossible).

    Reading checks the bounds of the parser. */
namespace QcAdvancementSerializer {

/*! Writes \a advancements to \a stream. */
void write(QDataStream &stream, const QcAdvancementCatalog &advancements);
/*! Reads the advancements from \a stream and adds them to \a registry. Returns an error for a
    corrupt or implausible table. */
[[nodiscard]] QcStatus read(QDataStream &stream, QcContentRegistry &registry);

} // namespace QcAdvancementSerializer
