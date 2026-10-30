// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QStringList>

class QcEntities;

/*! \brief The rules of the entity register, checked in one place (decision E-364).

    Tests call it after their scenarios; a broken rule is a bug. The rules:

    \list
    \li every id is given once, across all kinds and the players;
    \li every entity lies in the table of its kind (its kind field names that table);
    \li the records saved per chunk (QcEntityRecords::collect) put every entity into the chunk that
        holds its feet (QcEntities::chunkOf).
    \endlist */
namespace QcEntityChecks {

/*! Returns every broken rule of \a entities as a line naming the entity; empty when all hold. */
QStringList problems(const QcEntities &entities);

} // namespace QcEntityChecks
