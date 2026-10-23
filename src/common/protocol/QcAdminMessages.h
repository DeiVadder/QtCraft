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

// Bounds of the admin messages, checked by the decoders.
namespace AdminLimits {
inline constexpr qsizetype maxText = 256;
inline constexpr qsizetype maxLongText = 1024;
inline constexpr quint32 maxArguments = 4;
inline constexpr quint32 maxLines = 256;
inline constexpr quint32 maxPlayers = 1024;
inline constexpr quint32 maxEntries = 4096;
inline constexpr quint32 maxCommands = 1024;
inline constexpr quint32 maxAliases = 8;
inline constexpr quint32 maxSettings = 512;
// The status history: ten minutes at one point per second, a day at one point per minute.
inline constexpr quint32 maxHistory = 2048;
inline constexpr quint32 maxLogLines = 2000;
// What the server takes in the arguments of an action (checked there, offered to the screen):
// a reason of a kick or ban (QcAccessList::maxReasonLength) and a backup's label
// (QcWorldBackup::maxLabelLength).
inline constexpr qsizetype maxReason = 128;
inline constexpr qsizetype maxBackupLabel = 32;
} // namespace AdminLimits

// Client -> server: the topics the operator follows; 0 ends the subscription. Subscribing to a
// topic sends what came before (status history, recent chat, the log's tail at `logLevel` and
// above), then each new item.
struct AdminSubscribe
{
    static constexpr quint8 status = 1;
    static constexpr quint8 chat = 2;
    static constexpr quint8 log = 4;
    static constexpr quint8 all = status | chat | log;

    quint8 topics = 0;
    // Lowest level of the log lines (AdminLogLine::Level).
    quint8 logLevel = 1;
};

// Client -> server: one request of the operator; the server answers with AdminResult (same `id`)
// and, for the readers and the changes of a list or setting, the report of that topic.
struct AdminRequest
{
    enum class Kind : quint8 {
        // Reports (no arguments; LogTail: level, lines).
        Catalogue,
        Players,
        Lists,
        Settings,
        LogTail,
        // A command line as the operator typed it; the reply lines come back in AdminResult.
        Command,
        // A chat line to everyone as the server (/say).
        Chat,
        // Player management: name [reason].
        Kick,
        Ban,
        Unban,
        Op,
        Deop,
        WhitelistAdd,
        WhitelistRemove,
        // "on" or "off".
        Whitelist,
        // Game rule or server limit: name, value ("default" resets a limit).
        SetRule,
        SetLimit,
        Save,
        // [label]
        Backup,
        // Stops only with the argument "confirm".
        Stop,
    };
    static constexpr quint8 kindCount = static_cast<quint8>(Kind::Stop) + 1;

    quint32 id = 0;
    Kind kind = Kind::Players;
    QStringList arguments;
};

// Server -> client: the outcome of an AdminRequest (`id` 0 answers a subscription or ends it),
// with the command's replies as chat lines. A refusal has ok false and says why: the player is no
// operator (any subscription has ended then too), the arguments do not fit, stopping needs the
// confirmation, or the request came again too soon.
struct AdminResult
{
    enum class Refusal : quint8 { None, Rights, Arguments, Confirmation, TooSoon };
    static constexpr quint8 refusalCount = static_cast<quint8>(Refusal::TooSoon) + 1;

    quint32 id = 0;
    bool ok = false;
    QList<ChatMessage> lines;
    Refusal refusal = Refusal::None;
};

// One point of the status: at `timeMs` (milliseconds since the epoch, UTC) the ticks of the second
// before it, the players, chunks and entities, the resident memory and the network rates.
struct AdminSample
{
    qint64 timeMs = 0;
    float tickMeanMs = 0.0f;
    float tickP99Ms = 0.0f;
    float tickMaxMs = 0.0f;
    float ticksPerSecond = 0.0f;
    quint32 players = 0;
    quint32 loadedChunks = 0;
    quint32 entities = 0;
    float memoryMb = 0.0f;
    float sentKbPerSecond = 0.0f;
    float receivedKbPerSecond = 0.0f;

    bool operator==(const AdminSample &) const = default;
};

// Server -> client, about once a second while the operator follows the status.
struct AdminStatus
{
    AdminSample sample;
    // Mean time per tick phase over the latest ticks (QcTickPhaseTimer window).
    std::array<float, QcTickPhases::count> phaseMeanMs{};
    // Entities in the world by kind (QcEntityKind).
    std::array<quint32, QcEntityKinds::count> entities{};
    float peakMemoryMb = 0.0f;
    quint64 uptimeMs = 0;
    quint64 tick = 0;
    quint64 skippedTicks = 0;
    QString worldName;
    qint64 seed = 0;
    QString generator;
    // The server's program text (QcBuildInfo::clientText) and protocol version.
    QString version;
    quint32 protocol = 0;

    bool operator==(const AdminStatus &) const = default;
};

// Server -> client after subscribing to the status: the bounded history for charts, oldest first.
struct AdminHistory
{
    QList<AdminSample> seconds;
    QList<AdminSample> minutes;
};

// Server -> client: everyone playing, with the server's view of each.
struct AdminPlayers
{
    struct Entry
    {
        quint32 entityId = 0;
        QString name;
        // Round trip of the latest ping; -1 before the first answer.
        qint32 pingMs = -1;
        QcVec3 position;
        QcGameMode gameMode = QcGameMode::Survival;
        bool operatorRights = false;
        bool local = false;
        qint64 connectedMs = 0;
        QString address;

        bool operator==(const Entry &) const = default;
    };
    QList<Entry> players;
};

// One chat line as the server sent it to the players, with when (ms since the epoch) and who wrote
// it (empty for lines of the server).
struct AdminChatLine
{
    qint64 timeMs = 0;
    QString sender;
    ChatMessage message;
};

// Server -> client: recent chat after subscribing (`history`), then each new line.
struct AdminChat
{
    bool history = false;
    QList<AdminChatLine> lines;
};

// Server -> client: the commands of the registry and the macros of the content. `usage` and
// `description` are English source texts in the context "QcServerMessages" (macros: plain texts);
// QcCommandSyntax reads the parameters from `usage`.
struct AdminCatalogue
{
    struct Command
    {
        QString name;
        QStringList aliases;
        // QcPermission: 0 player, 1 operator, 2 console.
        quint8 permission = 0;
        bool needsPlayer = false;
        bool macro = false;
        QString usage;
        QString description;
    };
    QList<Command> commands;
};

// Server -> client: operators, whitelist and bans.
struct AdminLists
{
    struct Ban
    {
        QString name;
        QString reason;
        QString by;
        qint64 sinceMs = 0;
    };
    bool whitelistEnabled = false;
    QStringList operators;
    QStringList whitelist;
    QList<Ban> bans;
};

// Server -> client: game rules and server limits with what a settings page needs.
struct AdminSettings
{
    enum class Type : quint8 { Boolean, Integer };
    struct Setting
    {
        QString key;
        // Translation keys: rules in the context "QcGameRules", limits in "QcServerLimits".
        QString group;
        QString description;
        Type type = Type::Integer;
        qint64 low = 0;
        qint64 high = 0;
        qint64 defaultValue = 0;
        qint64 value = 0;

        bool operator==(const Setting &) const = default;
    };
    QList<Setting> rules;
    QList<Setting> limits;
    // Where changed limits are written: false world.json, true server.json.
    bool limitsFromServer = false;
};

// One line of the server's log (QcMessageLog::recent).
struct AdminLogLine
{
    enum class Level : quint8 { Debug, Info, Warning, Critical, Fatal };
    static constexpr quint8 levelCount = 5;

    qint64 timeMs = 0;
    Level level = Level::Info;
    QString category;
    QString text;
};

// Server -> client: the log's tail (`tail`, answering a subscription or LogTail) or new lines.
struct AdminLog
{
    bool tail = false;
    QList<AdminLogLine> lines;
};

} // namespace QcMsg
