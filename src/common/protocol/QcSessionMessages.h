#pragma once

// Protocol messages of the connection and session: handshake, content, chat and world time.
// Part of the QcMessage variant (QcMessages.h).

#include "QcAppearance.h"
#include "QcContentRegistry.h"

#include <QString>
#include <QStringList>

#include <memory>

namespace QcMsg {

// Client -> server: first message on every connection.
struct Hello
{
    // Longer names are refused by the decoder before the server cleans them (32 code points).
    static constexpr qsizetype maxNameLength = 256;

    quint32 protocolVersion = 0;
    QString playerName;
    QcAppearance appearance;
};

// Server -> client: accepted connection.
struct Welcome
{
    static constexpr qint32 maxViewDistance = 64;

    quint32 protocolVersion = 0;
    quint32 playerEntityId = 0;
    QString worldName;
    qint64 seed = 0;
    // View direction from the save; only meaningful when `restoredView` is set.
    float yaw = 0.0f;
    float pitch = 0.0f;
    bool restoredView = false;
    // Chunks the server streams around this player (at most its limit).
    qint32 viewDistance = 0;
};

// Server -> client: all content definitions; runtime block ids are the indices in this registry.
struct ContentSync
{
    std::shared_ptr<const QcContentRegistry> content;
};

// Both directions: the connection ends. `reason` is the English text; a non-empty `key` with its
// `arguments` lets the client show it translated like a chat line (QcChatText, decision E-094).
struct Disconnect
{
    static constexpr quint32 maxArguments = 16;

    QString reason;
    QString key;
    QStringList arguments;
};

// Client -> server: chat line or command starting with '/'.
struct ChatInput
{
    QString text;
};

// Server -> client: chat line or command feedback. `text` is the English line; a non-empty `key`
// with its `arguments` lets the client show it translated (QcChatText, decision E-094).
struct ChatMessage
{
    static constexpr quint32 maxArguments = 16;

    QString text;
    QString key;
    QStringList arguments;
};

// Server -> client: seconds since the world began and the length of a full day; `running` is false
// while the time of day stands still (game rule daylightCycle, decision E-221). `speed` is how many
// server ticks pass per tick of the client's clock (tick rate / 20, the speed of a running sprint;
// 0 while the server is frozen; decision E-249); the client advances the time of day by it between
// two messages.
struct WorldTime
{
    // A sprint runs as many ticks as the server manages, thousands per second in a small world.
    static constexpr double maxSpeed = 100'000.0;

    double time = 0.0;
    double dayLength = 0.0;
    bool running = true;
    double speed = 1.0;
};

} // namespace QcMsg
