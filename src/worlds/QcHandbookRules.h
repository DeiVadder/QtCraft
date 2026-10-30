// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QMap>
#include <QString>

class QcContentRegistry;

/*! \brief Rule values of the server and the shared physics that handbook chapters quote as
    {name} and creature pages show as caps (QcHandbookBuilder), so the handbook follows the code
    and, where a value belongs to a block of the base content (the explosive), that content. */
namespace QcHandbookRules {

/*! Returns the rule values by name (\c {day.length}, \c {mobs.hostileCap}, ...); the explosion
    values follow the explosive block of \a content. */
QMap<QString, double> values(const QcContentRegistry &content);

} // namespace QcHandbookRules
