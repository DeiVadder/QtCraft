// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcCommandCategory.h"
#include "common/core/QcResult.h"
#include "server/commands/QcCommandSource.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

/*! \brief One call of a command: who runs it and the words after the command's name. */
struct QcCommandContext
{
    /*! Who runs the command. */
    const QcCommandSource &source;
    /*! The words after the command's name. */
    const QStringList &args;
    /*! The whole line as typed. */
    const QString &text;
    /*! The right the command needs (a confirmation checks it again). */
    QcPermission permission = QcPermission::Player;

    /*! Returns the player who runs it. Precondition: the command needs a player
        (QcCommandSpec::needsPlayer). */
    QcServerClient &player() const { return *source.player(); }
    /*! Replies to the source with the translatable \a key and \a arguments
        (QcCommandSource::reply()). */
    void reply(const char *key, const QStringList &arguments = {}) const { source.reply(key, arguments); }
};

/*! Runs a command for the call \c {context}. */
using QcCommandHandler = std::function<void(const QcCommandContext &context)>;

/*! \brief A command as the registry knows it (decision E-243).

    \c {usage} and \c {description} are English texts marked with
    QT_TRANSLATE_NOOP("QcServerMessages", ...); /help shows them translated. */
struct QcCommandSpec
{
    /*! The command's name. Every field has an initializer, so designated initializers may leave
        any out. */
    QString name = {};
    /*! The group /help lists it under (decision E-911). */
    QcCommandCategory category = QcCommandCategory::Other;
    /*! Other names that run the command. */
    QStringList aliases = {}; // NOLINT(readability-redundant-member-init): see the comment on `name`
    /*! The right the command needs. */
    QcPermission permission = QcPermission::Operator;
    /*! Runs only for players (it acts on the player or around him); the console gets a refusal. */
    bool needsPlayer = false;
    /*! Fewer words after the name answer with the usage. */
    qsizetype minArguments = 0;
    /*! The usage line ("Usage: /name <placeholder> [optional]"). */
    const char *usage = nullptr;
    /*! What the command does, one sentence. */
    const char *description = nullptr;
    /*! What runs the command. */
    QcCommandHandler run = {}; // NOLINT(readability-redundant-member-init): see the comment on `name`
    /*! The words a placeholder takes that the content and the world decide, not the usage (the
        structures of /locate, the biomes of /locatebiome; decision E-911): /help <command> names
        them in \c {namesLine} (a key with %1), the admin channel's catalogue sends them for
        completion. \qcshared */
    std::function<QStringList()> names = {}; // NOLINT(readability-redundant-member-init): see the comment on `name`
    const char *namesLine = nullptr;
};

/*! \brief All commands of a server, by name and alias.

    The game's commands register here like any later C++ extension would (docs/Modding.md);
    data-only mods add macros instead (decision E-248). */
class QcCommandRegistry
{
public:
    /*! The longest command name. */
    static constexpr qsizetype maxNameLength = 32;

    /*! Returns whether \a name is a valid command name: lower-case letters, digits and '_', at
        most maxNameLength. */
    static bool isValidName(QStringView name);

    /*! Adds the command \a spec. Returns an error for invalid names, a missing handler or texts,
        and names or aliases that are taken. Pointers from find() and commands() stay valid until
        the next add. */
    [[nodiscard]] QcStatus add(QcCommandSpec spec);
    /*! Adds \a spec, one of the game's own commands, whose names are known to be free: a failure
        is a programming error and only logged. */
    void addBuiltIn(QcCommandSpec spec);
    /*! Returns the command named \a name by name or alias (lower case); nullptr when there is
        none. */
    const QcCommandSpec *find(QStringView name) const;
    /*! Returns every command, sorted by name. */
    QList<const QcCommandSpec *> commands() const;

private:
    QList<QcCommandSpec> m_specs;
    // Name or alias -> index in m_specs.
    QHash<QString, qsizetype> m_names;
};
