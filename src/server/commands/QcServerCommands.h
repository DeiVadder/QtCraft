#pragma once

#include "QcBlockCommands.h"
#include "QcCheatCommands.h"
#include "QcCommandRegistry.h"
#include "QcPendingCommands.h"
#include "QcPlayerCommands.h"
#include "QcTickCommands.h"
#include "QcWorldCommands.h"

#include <QString>

class QcBlockUpdateQueue;
class QcServerBlocks;
struct QcCommandMacroDefinition;
struct QcServerClient;
struct QcServerState;

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
    };

    QcServerCommands(QcServerState &state, Parts parts);

    // A chat line of a playing client (at most 256 characters).
    void chat(QcServerClient &client, const QString &text);
    // A line of the console; the leading '/' is optional. Replies go to `output`.
    void console(const QString &line, const QcCommandSource::Output &output);
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
    QcStatus add(QcCommandSpec spec) { return m_registry.add(std::move(spec)); }

private:
    // Macros run their lines with `depth` 1 and cannot start further macros.
    void execute(const QcCommandSource &source, const QString &text, int depth);
    void runMacro(const QcCommandSource &source, const QcCommandMacroDefinition &macro, const QStringList &args);
    bool permitted(const QcCommandSource &source, QcPermission needed, const QString &name) const;
    void addBuiltIns();
    // /help lists and explains only what the source may run.
    void help(const QcCommandContext &context) const;
    static bool mayRun(const QcCommandSource &source, const QcCommandMacroDefinition &macro);
    void broadcast(const QcServerClient &client, const QString &text) const;

    QcServerState &m_state;
    QcCommandRegistry m_registry;
    QcPendingCommands m_pending;
    QcCheatCommands m_cheats;
    QcBlockCommands m_blocks;
    QcPlayerCommands m_players;
    QcWorldCommands m_world;
    QcTickCommands m_ticks;
};
