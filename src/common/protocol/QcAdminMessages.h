// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Protocol messages of the admin channel (decision E-848): an operator who plays on the server asks
// for its status, players, chat, commands, lists, settings and log and acts on them over his game
// connection. The server checks the operator's rights on every message. Part of the QcMessage
// variant (QcMessages.h).

#include "common/core/QcTickPhase.h"
#include "common/core/QcVec3.h"
#include "common/gameplay/QcGameMode.h"
#include "common/protocol/QcSessionMessages.h"
#include "common/world/QcEntityKind.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <array>

namespace QcMsg {

/*! \brief Bounds of the admin messages, checked by the decoders. */
namespace AdminLimits {
/*! Characters of a short text (names, categories, arguments): the encoders cut a longer one, the
    decoders refuse it. */
inline constexpr qsizetype maxText = 256;
/*! Characters of a long text (usages, descriptions, chat and log line texts). */
inline constexpr qsizetype maxLongText = 1024;
/*! Arguments of an AdminRequest. */
inline constexpr quint32 maxArguments = 4;
/*! Chat lines of an AdminResult or AdminChat. */
inline constexpr quint32 maxLines = 256;
/*! Players of an AdminPlayers. */
inline constexpr quint32 maxPlayers = 1024;
/*! Names of each list of an AdminLists (operators, whitelist, bans). */
inline constexpr quint32 maxEntries = 4096;
/*! Commands and macros of an AdminCatalogue. */
inline constexpr quint32 maxCommands = 1024;
/*! Aliases of a command of an AdminCatalogue. */
inline constexpr quint32 maxAliases = 8;
/*! The words a command's placeholder takes beyond its usage (structures, biomes; decision
    E-911). */
inline constexpr quint32 maxNames = 256;
/*! Game rules, and server limits, of an AdminSettings. */
inline constexpr quint32 maxSettings = 512;
/*! The status history: ten minutes at one point per second, a day at one point per minute. */
inline constexpr quint32 maxHistory = 2048;
/*! Lines of an AdminLog. */
inline constexpr quint32 maxLogLines = 2000;
/*! What the server takes in the arguments of an action (checked there, offered to the screen):
    a reason of a kick or ban (QcAccessList::maxReasonLength) and a backup's label
    (QcWorldBackup::maxLabelLength). \qcshared */
inline constexpr qsizetype maxReason = 128;
inline constexpr qsizetype maxBackupLabel = 32;
} // namespace AdminLimits

/*! \brief Client -> server: the topics the operator follows; 0 ends the subscription.

    Subscribing to a topic sends what came before (status history, recent chat, the log's tail at
    \c {logLevel} and above), then each new item. */
struct AdminSubscribe
{
    /*! Topic bit: the status (AdminHistory, AdminStatus, AdminPlayers). */
    static constexpr quint8 status = 1;
    /*! Topic bit: the chat (AdminChat). */
    static constexpr quint8 chat = 2;
    /*! Topic bit: the log (AdminLog). */
    static constexpr quint8 log = 4;
    /*! All topic bits. */
    static constexpr quint8 all = status | chat | log;

    /*! The topic bits followed; other bits make the message invalid. */
    quint8 topics = 0;
    /*! Lowest level of the log lines (AdminLogLine::Level). */
    quint8 logLevel = 1;
};

/*! \brief Client -> server: one request of the operator; the server answers with AdminResult
    (same \c {id}) and, for the readers and the changes of a list or setting, the report of that
    topic. */
struct AdminRequest
{
    /*! The requests and their arguments. */
    enum class Kind : quint8 {
        /*! Asks for AdminCatalogue (no arguments). */
        Catalogue,
        /*! Asks for AdminPlayers (no arguments). */
        Players,
        /*! Asks for AdminLists (no arguments). */
        Lists,
        /*! Asks for AdminSettings (no arguments). */
        Settings,
        /*! Asks for the log's tail as AdminLog: level, number of lines (both optional); at most
            once a second. */
        LogTail,
        /*! A command line as the operator typed it; the reply lines come back in AdminResult. */
        Command,
        /*! A chat line to everyone as the server (/say). */
        Chat,
        /*! Kicks a player: name [reason]. */
        Kick,
        /*! Bans a player: name [reason]. */
        Ban,
        /*! Lifts a ban: name. */
        Unban,
        /*! Gives a player operator rights: name. */
        Op,
        /*! Takes a player's operator rights: name. */
        Deop,
        /*! Adds a name to the whitelist: name. */
        WhitelistAdd,
        /*! Removes a name from the whitelist: name. */
        WhitelistRemove,
        /*! Switches the whitelist: "on" or "off". */
        Whitelist,
        /*! Sets a game rule: name, value. */
        SetRule,
        /*! Sets a server limit: name, value ("default" resets it). */
        SetLimit,
        /*! Saves the world (no arguments). */
        Save,
        /*! Makes a backup of the world: [label]. */
        Backup,
        /*! Stops only with the argument "confirm". */
        Stop,
    };
    /*! The number of request kinds. */
    static constexpr quint8 kindCount = static_cast<quint8>(Kind::Stop) + 1;

    /*! The number the client picked; AdminResult answers with it. */
    quint32 id = 0;
    /*! What is asked. */
    Kind kind = Kind::Players;
    /*! The arguments of the kind (at most AdminLimits::maxArguments). */
    QStringList arguments;
};

/*! \brief Server -> client: the outcome of an AdminRequest (\c {id} 0 answers a subscription or
    ends it), with the command's replies as chat lines.

    A refusal has ok false and says why: the player is no operator (any subscription has ended
    then too), the arguments do not fit, stopping needs the confirmation, or the request came
    again too soon. */
struct AdminResult
{
    /*! Why a request was refused. */
    enum class Refusal : quint8 {
        /*! Not refused. */
        None,
        /*! The player is no operator. */
        Rights,
        /*! The arguments do not fit the request. */
        Arguments,
        /*! Stopping needs the argument "confirm". */
        Confirmation,
        /*! The request came again too soon (the log's tail: at most once a second). */
        TooSoon
    };
    /*! The number of refusals. */
    static constexpr quint8 refusalCount = static_cast<quint8>(Refusal::TooSoon) + 1;

    /*! The \c {id} of the AdminRequest answered; 0 for a subscription. */
    quint32 id = 0;
    /*! Whether the request was carried out. */
    bool ok = false;
    /*! The command's replies, or the reason of a refusal. */
    QList<ChatMessage> lines;
    /*! Why the request was refused; None when it was not. */
    Refusal refusal = Refusal::None;
};

/*! \brief One point of the status: at \c {timeMs} (milliseconds since the epoch, UTC) the ticks
    of the second before it, the players, chunks and entities, the resident memory and the
    network rates. */
struct AdminSample
{
    /*! When the sample was taken, in milliseconds since the epoch (UTC). */
    qint64 timeMs = 0;
    /*! Mean duration of a tick in the second, in milliseconds. */
    float tickMeanMs = 0.0f;
    /*! 99th percentile of the tick durations in the second, in milliseconds. */
    float tickP99Ms = 0.0f;
    /*! Longest tick in the second, in milliseconds. */
    float tickMaxMs = 0.0f;
    /*! Ticks run in the second, per second of real time. */
    float ticksPerSecond = 0.0f;
    /*! Players who have joined. */
    quint32 players = 0;
    /*! Chunks loaded in the world. */
    quint32 loadedChunks = 0;
    /*! Entities in the world. */
    quint32 entities = 0;
    /*! Resident memory of the server process in MiB (QcProcessMemory::currentMb). */
    float memoryMb = 0.0f;
    /*! KiB per second the server sent to network clients. */
    float sentKbPerSecond = 0.0f;
    /*! KiB per second the server received from network clients. */
    float receivedKbPerSecond = 0.0f;

    bool operator==(const AdminSample &) const = default;
};

/*! \brief Server -> client, about once a second while the operator follows the status. */
struct AdminStatus
{
    /*! The latest sample. */
    AdminSample sample;
    /*! Mean time per tick phase over the latest ticks (QcTickPhaseTimer window). */
    std::array<float, QcTickPhases::count> phaseMeanMs{};
    /*! Entities in the world by kind (QcEntityKind). */
    std::array<quint32, QcEntityKinds::count> entities{};
    /*! Peak resident memory of the server process in MiB (QcProcessMemory::peakMb). */
    float peakMemoryMb = 0.0f;
    /*! Milliseconds since the server was created. */
    quint64 uptimeMs = 0;
    /*! The current tick of the world clock. */
    quint64 tick = 0;
    /*! Ticks the server dropped so far (QcTickStats::skipped). */
    quint64 skippedTicks = 0;
    /*! The world's name. */
    QString worldName;
    /*! The world's seed. */
    qint64 seed = 0;
    /*! The name of the world's generator. */
    QString generator;
    /*! The server's program text (QcBuildInfo::clientText) and protocol version. \qcshared */
    QString version;
    quint32 protocol = 0;

    bool operator==(const AdminStatus &) const = default;
};

/*! \brief Server -> client after subscribing to the status: the bounded history for charts,
    oldest first. */
struct AdminHistory
{
    /*! One sample per second (QcAdminSamples::secondsKept at most). */
    QList<AdminSample> seconds;
    /*! One sample per minute, folded from its seconds (QcAdminSamples::minutesKept at most). */
    QList<AdminSample> minutes;
};

/*! \brief Server -> client: everyone playing, with the server's view of each. */
struct AdminPlayers
{
    /*! \brief One player as the server sees him. */
    struct Entry
    {
        /*! The player's entity. */
        quint32 entityId = 0;
        /*! The player's name. */
        QString name;
        /*! Round trip of the latest ping; -1 before the first answer. */
        qint32 pingMs = -1;
        /*! Where the player stands. */
        QcVec3 position;
        /*! The player's game mode. */
        QcGameMode gameMode = QcGameMode::Survival;
        /*! Whether the player has operator rights. */
        bool operatorRights = false;
        /*! Whether the player plays in the server's own process (single player host). */
        bool local = false;
        /*! Milliseconds since the player connected. */
        qint64 connectedMs = 0;
        /*! The network address the player connected from. */
        QString address;

        bool operator==(const Entry &) const = default;
    };
    /*! The players who play (at most AdminLimits::maxPlayers). */
    QList<Entry> players;
};

/*! \brief One chat line as the server sent it to the players, with when (ms since the epoch) and
    who wrote it (empty for lines of the server). */
struct AdminChatLine
{
    /*! When the line was sent, in milliseconds since the epoch. */
    qint64 timeMs = 0;
    /*! Who wrote it; empty for lines of the server. */
    QString sender;
    /*! The line. */
    ChatMessage message;
};

/*! \brief Server -> client: recent chat after subscribing (\c {history}), then each new line. */
struct AdminChat
{
    /*! Whether the lines are the recent chat sent after subscribing. */
    bool history = false;
    /*! The lines, oldest first. */
    QList<AdminChatLine> lines;
};

/*! \brief Server -> client: the commands of the registry and the macros of the content.

    \c {usage} and \c {description} are English source texts in the context "QcServerMessages"
    (macros: plain texts); QcCommandSyntax reads the parameters from \c {usage}. */
struct AdminCatalogue
{
    /*! \brief One command or macro. */
    struct Command
    {
        /*! The command's name without the slash. */
        QString name;
        /*! Other names of the command (at most AdminLimits::maxAliases). */
        QStringList aliases;
        /*! QcPermission: 0 player, 1 operator, 2 console. */
        quint8 permission = 0;
        /*! Whether the command runs only for players (the console gets a refusal). */
        bool needsPlayer = false;
        /*! Whether it is a macro of the content rather than a command of the registry. */
        bool macro = false;
        /*! The usage line (see the struct). */
        QString usage;
        /*! What the command does (see the struct). */
        QString description;
        /*! QcCommandCategory (decision E-911). */
        quint8 category = 0;
        /*! What its <structure> or <biome> takes in this world (QcCommandSpec::names). */
        QStringList names;
    };
    /*! The commands, then the macros (at most AdminLimits::maxCommands). */
    QList<Command> commands;
};

/*! \brief Server -> client: operators, whitelist and bans. */
struct AdminLists
{
    /*! \brief One ban. */
    struct Ban
    {
        /*! The banned player's name. */
        QString name;
        /*! Why, as the operator gave it; may be empty. */
        QString reason;
        /*! Who banned the player. */
        QString by;
        /*! Since when, in milliseconds since the epoch; 0 when unknown. */
        qint64 sinceMs = 0;
    };
    /*! Whether the whitelist is on. */
    bool whitelistEnabled = false;
    /*! The names of the operators. */
    QStringList operators;
    /*! The names on the whitelist. */
    QStringList whitelist;
    /*! The bans. */
    QList<Ban> bans;
};

/*! \brief Server -> client: game rules and server limits with what a settings page needs. */
struct AdminSettings
{
    /*! The kinds of value of a setting. */
    enum class Type : quint8 {
        /*! On (1) or off (0). */
        Boolean,
        /*! A whole number between \c {low} and \c {high}. */
        Integer
    };
    /*! \brief One game rule or server limit. */
    struct Setting
    {
        /*! The name commands use for it. */
        QString key;
        /*! Translation keys: rules in the context "QcGameRules", limits in "QcServerLimits".
            \qcshared */
        QString group;
        QString description;
        /*! The kind of value. */
        Type type = Type::Integer;
        /*! The lowest and highest value allowed. \qcshared */
        qint64 low = 0;
        qint64 high = 0;
        /*! The value it has when not set. */
        qint64 defaultValue = 0;
        /*! The value it has now. */
        qint64 value = 0;

        bool operator==(const Setting &) const = default;
    };
    /*! The game rules of the world. */
    QList<Setting> rules;
    /*! The server limits. */
    QList<Setting> limits;
    /*! Where changed limits are written: false world.json, true server.json. */
    bool limitsFromServer = false;
};

/*! \brief One line of the server's log (QcMessageLog::recent). */
struct AdminLogLine
{
    /*! The levels of a log line, as those of Qt's messages. */
    enum class Level : quint8 {
        /*! qDebug */
        Debug,
        /*! qInfo */
        Info,
        /*! qWarning */
        Warning,
        /*! qCritical */
        Critical,
        /*! qFatal */
        Fatal
    };
    /*! The number of levels. */
    static constexpr quint8 levelCount = 5;

    /*! When the line was written, in milliseconds since the epoch. */
    qint64 timeMs = 0;
    /*! The line's level. */
    Level level = Level::Info;
    /*! The logging category. */
    QString category;
    /*! The message. */
    QString text;
};

/*! \brief Server -> client: the log's tail (\c {tail}, answering a subscription or LogTail) or
    new lines. */
struct AdminLog
{
    /*! Whether the lines are a tail that replaces what the client shows. */
    bool tail = false;
    /*! The lines, oldest first. */
    QList<AdminLogLine> lines;
};

} // namespace QcMsg
