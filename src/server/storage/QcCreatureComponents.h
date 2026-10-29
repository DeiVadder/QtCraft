// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "server/storage/QcComponentFrames.h"
#include "server/storage/QcEntityRecord.h"

#include <QDataStream>

#include <optional>

// The components of a creature entry in the entities of a chunk (QcEntityCodec, decision E-420):
// health and breeding (Creature), the villager record, status effects, the carried block, the
// enclosure, the golem's builder, the laying timer, the tame state, a creature without a mind and the
// parent of a child of a kept family (decision E-839).
namespace QcCreatureComponents {

// A creature read from its components, the carried block still by name (the caller resolves it
// against the content and reports one it no longer has), and the components this version does not know.
struct Read
{
    QcMobManager::Stored mob;
    QString carried;
    QcComponentFrames::ReadComponents components;
};

// Reads the components of a creature entry onto `mob` (filled from the entry's header, health at the
// type's full health); std::nullopt for damaged data.
[[nodiscard]] std::optional<Read> read(QDataStream &stream, QcMobManager::Stored mob);

// Adds the known components of `mob` to the writer of its entry and returns that writer (the generic
// writer of QcComponentFrames, extended from outside: rank 3 of the rule on output parameters).
[[nodiscard]] QcComponentFrames::Writer &write(QcComponentFrames::Writer &components, const QcMobManager::Stored &mob,
                                               const QcContentRegistry &content);

} // namespace QcCreatureComponents
