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
    quint32 protocolVersion = 0;
    QString playerName;
    QcAppearance appearance;
};

// Server -> client: accepted connection.
struct Welcome
{
    quint32 protocolVersion = 0;
    quint32 playerEntityId = 0;
    QString worldName;
    qint64 seed = 0;
    // View direction from the save; only meaningful when `restoredView` is set.
    float yaw = 0.0f;
    float pitch = 0.0f;
    bool restoredView = false;
};

// Server -> client: all content definitions; runtime block ids are the indices in this registry.
struct ContentSync
{
    std::shared_ptr<const QcContentRegistry> content;
};

struct Disconnect
{
    QString reason;
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
// while the time of day stands still (game rule daylightCycle, decision E-221).
struct WorldTime
{
    double time = 0.0;
    double dayLength = 0.0;
    bool running = true;
};

} // namespace QcMsg
