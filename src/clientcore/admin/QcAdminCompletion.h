// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcCommandSyntax.h"
#include "common/protocol/QcAdminMessages.h"

#include <QStringList>

class QcContentRegistry;

// Completion of a command line for the admin channel's command picker (decision E-848): the
// command names of the catalogue for the first word, then what the parameter at the cursor takes
// by its usage (QcCommandSyntax): the choices, the names of the players, the structures or biomes
// the server names with the command (decision E-911), or the blocks, items and
// creatures of the content. Pure: everything it offers comes from its arguments.
namespace QcAdminCompletion {

struct Sources
{
    const QcMsg::AdminCatalogue &catalogue;
    QStringList players;
    // May be null; then blocks, items and creatures offer nothing.
    const QcContentRegistry *content = nullptr;
};

// The whole lines `line` may become by completing its last word (a trailing space starts a new
// word), sorted, at most `limit`.
QStringList complete(const QString &line, const Sources &sources, qsizetype limit = 50);
// The parameters the next word of `line` may be, by the forms of its command's usage.
QList<QcCommandSyntax::Parameter> parameters(const QString &line, const QcMsg::AdminCatalogue &catalogue);
// The catalogue's command named `name` (or with that alias); nullptr when there is none.
const QcMsg::AdminCatalogue::Command *command(const QcMsg::AdminCatalogue &catalogue, QStringView name);

} // namespace QcAdminCompletion
