// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcCommandSyntax.h"
#include "common/protocol/QcAdminMessages.h"

#include <QStringList>

class QcContentRegistry;

/*! \brief Completion of a command line for the admin channel's command picker (decision E-848): the
    command names of the catalogue for the first word, then what the parameter at the cursor takes
    by its usage (QcCommandSyntax): the choices, the names of the players, the structures or biomes
    the server names with the command (decision E-911), or the blocks, items and creatures of the
    content.

    Pure: everything it offers comes from its arguments. */
namespace QcAdminCompletion {

/*! \brief What completion takes its words from. */
struct Sources
{
    /*! The server's commands with their usage, for command names and parameters. */
    const QcMsg::AdminCatalogue &catalogue;
    /*! The names of the players on the server. */
    QStringList players;
    /*! May be null; then blocks, items and creatures offer nothing. */
    const QcContentRegistry *content = nullptr;
};

/*! Returns the whole lines \a line may become by completing its last word from \a sources (a
    trailing space starts a new word), sorted, at most \a limit. */
QStringList complete(const QString &line, const Sources &sources, qsizetype limit = 50);
/*! Returns the parameters the next word of \a line may be, by the forms of its command's usage
    in \a catalogue. */
QList<QcCommandSyntax::Parameter> parameters(const QString &line, const QcMsg::AdminCatalogue &catalogue);
/*! Returns the command of \a catalogue named \a name (or with that alias); nullptr when there is
    none. */
const QcMsg::AdminCatalogue::Command *command(const QcMsg::AdminCatalogue &catalogue, QStringView name);

} // namespace QcAdminCompletion
