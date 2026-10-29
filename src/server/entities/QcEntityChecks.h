// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QStringList>

class QcEntities;

// The rules of the entity register, checked in one place (decision E-364; tests call it after their
// scenarios, a broken rule is a bug):
// - every id is given once, across all kinds and the players;
// - every entity lies in the table of its kind (its kind field names that table);
// - the records saved per chunk (QcEntityRecords::collect) put every entity into the chunk that
//   holds its feet (QcEntities::chunkOf).
namespace QcEntityChecks {

// Every broken rule as a line naming the entity; empty when all hold.
QStringList problems(const QcEntities &entities);

} // namespace QcEntityChecks
