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

// One call of a command: who runs it and the words after the command's name.
struct QcCommandContext
{
    const QcCommandSource &source;
    const QStringList &args;
    // The whole line as typed.
    const QString &text;
    // The right the command needs (a confirmation checks it again).
    QcPermission permission = QcPermission::Player;

    // The player who runs it. Precondition: the command needs a player (QcCommandSpec::needsPlayer).
    QcServerClient &player() const { return *source.player(); }
    void reply(const char *key, const QStringList &arguments = {}) const { source.reply(key, arguments); }
};

using QcCommandHandler = std::function<void(const QcCommandContext &context)>;

// A command as the registry knows it (decision E-243). `usage` and `description` are English texts
// marked with QT_TRANSLATE_NOOP("QcServerMessages", ...); /help shows them translated.
struct QcCommandSpec
{
    // Every field has an initializer, so designated initializers may leave any out.
    QString name = {};
    // The group /help lists it under (decision E-911).
    QcCommandCategory category = QcCommandCategory::Other;
    QStringList aliases = {}; // NOLINT(readability-redundant-member-init): see the comment on `name`
    QcPermission permission = QcPermission::Operator;
    // Runs only for players (it acts on the player or around him); the console gets a refusal.
    bool needsPlayer = false;
    // Fewer words after the name answer with the usage.
    qsizetype minArguments = 0;
    const char *usage = nullptr;
    const char *description = nullptr;
    QcCommandHandler run = {}; // NOLINT(readability-redundant-member-init): see the comment on `name`
    // The words a placeholder takes that the content and the world decide, not the usage (the
    // structures of /locate, the biomes of /locatebiome; decision E-911): /help <command> names them
    // in `namesLine` (a key with %1), the admin channel's catalogue sends them for completion.
    std::function<QStringList()> names = {}; // NOLINT(readability-redundant-member-init): see the comment on `name`
    const char *namesLine = nullptr;
};

// All commands of a server, by name and alias. The game's commands register here like any later
// C++ extension would (docs/Modding.md); data-only mods add macros instead (decision E-248).
class QcCommandRegistry
{
public:
    static constexpr qsizetype maxNameLength = 32;

    // Lower-case letters, digits and '_', at most maxNameLength.
    static bool isValidName(QStringView name);

    // Fails for invalid names, a missing handler or texts, and names or aliases that are taken.
    // Pointers from find() and commands() stay valid until the next add.
    [[nodiscard]] QcStatus add(QcCommandSpec spec);
    // For the game's own commands, whose names are known to be free: a failure is a programming
    // error and only logged.
    void addBuiltIn(QcCommandSpec spec);
    // By name or alias (lower case); nullptr when there is none.
    const QcCommandSpec *find(QStringView name) const;
    // Every command, sorted by name.
    QList<const QcCommandSpec *> commands() const;

private:
    QList<QcCommandSpec> m_specs;
    // Name or alias -> index in m_specs.
    QHash<QString, qsizetype> m_names;
};
