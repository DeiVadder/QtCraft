// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/commands/QcAdvancementCommands.h"
#include "server/commands/QcAdventureCommands.h"
#include "server/commands/QcBlockCommands.h"
#include "server/commands/QcChatLog.h"
#include "server/commands/QcCheatCommands.h"
#include "server/commands/QcChunkCommands.h"
#include "server/commands/QcCommandHelp.h"
#include "server/commands/QcCommandRegistry.h"
#include "server/commands/QcEffectCommands.h"
#include "server/commands/QcExperienceCommands.h"
#include "server/commands/QcLimitCommands.h"
#include "server/commands/QcMobCommands.h"
#include "server/commands/QcPendingCommands.h"
#include "server/commands/QcPlayerCommands.h"
#include "server/commands/QcTickCommands.h"
#include "server/commands/QcVillageCommands.h"
#include "server/commands/QcWeatherCommands.h"
#include "server/commands/QcWorldCommands.h"

#include <QString>

class QcBlockUpdateQueue;
class QcServerBlocks;
class QcServerWeather;
struct QcCommandMacroDefinition;
struct QcServerClient;
class QcServerState;

// The command layer of the server (decision E-243), shared by the chat of every player and the
// server console: plain chat lines go to everyone, lines starting with '/' run a command of the
// registry or a macro of the content (decision E-248) after the source's rights are checked. Every
// command is logged with its source (category qtcraft.commands). /help, /confirm and /cancel are
// built in.
class QcServerCommands
{
public:
    struct Parts
    {
        QcServerBlocks &blocks;
        QcBlockUpdateQueue &updates;
        QcCheatCommands::Parts cheats;
        QcWorldCommands::Parts world;
        QcTickCommands::Parts ticks;
        QcServerWeather &weather;
    };

    QcServerCommands(QcServerState &state, Parts parts);

    // A chat line of a playing client (at most 256 characters).
    void chat(QcServerClient &client, const QString &text);
    // A line of the console; the leading '/' is optional. Replies go to `output`.
    void console(const QString &line, const QcCommandSource::Output &output);
    // A command line of a player through the admin channel (decision E-848): checked and run like
    // his chat command (the leading '/' is optional), the replies go to `output` instead of his chat.
    // The line is not cut to the chat's 256 characters.
    void admin(QcServerClient &client, const QString &line, const QcCommandSource::Output &output);
    // The chat lines everyone saw (players' chat and /say).
    const QcChatLog &chatLog() const { return m_chatLog; }
    // After the server's due ticks: reports a sprint that ended.
    void afterTicks() { m_ticks.reportSprintEnd(); }
    // Every tick: reports a backup copied in the background (decision E-256).
    void pollBackups() { m_world.pollBackup(); }
    // Server stop: waits for a backup still being copied.
    void waitForBackups() { m_world.waitForBackup(); }
    QcBackupRunner &backups() { return m_world.backups(); }
    // A player left: his pending confirmation goes.
    void playerLeft(quint32 entityId) { m_pending.forget(entityId); }

    const QcCommandRegistry &registry() const { return m_registry; }
    // For C++ extensions (docs/Modding.md): adds a command; fails for taken names.
    [[nodiscard]] QcStatus add(QcCommandSpec spec) { return m_registry.add(std::move(spec)); }

private:
    // Macros run their lines with `depth` 1 and cannot start further macros.
    void execute(const QcCommandSource &source, const QString &text, int depth);
    void runMacro(const QcCommandSource &source, const QcCommandMacroDefinition &macro, const QStringList &args);
    bool permitted(const QcCommandSource &source, QcPermission needed, const QString &name) const;
    void addBuiltIns();
    void broadcast(const QcServerClient &client, const QString &text);
    // /say: a line to everyone as the server.
    void say(const QcCommandContext &context);
    // Sends `line` to every playing client and keeps it in the chat log.
    void sendToAll(const QString &sender, const QcMsg::ChatMessage &line);

    QcServerState &m_state;
    QcCommandRegistry m_registry;
    QcPendingCommands m_pending;
    QcChatLog m_chatLog;
    // Before the cheats, which take the parts it reads.
    QcAdventureCommands m_adventure;
    QcCheatCommands m_cheats;
    QcBlockCommands m_blocks;
    QcPlayerCommands m_players;
    QcWorldCommands m_world;
    QcTickCommands m_ticks;
    QcChunkCommands m_chunks;
    QcWeatherCommands m_weather;
    QcVillageCommands m_villages;
    QcExperienceCommands m_experience;
    QcAdvancementCommands m_advancements;
    QcEffectCommands m_effects;
    QcMobCommands m_mobs;
    QcLimitCommands m_limits;
    // /help lists and explains only what the source may run.
    QcCommandHelp m_help;
};
