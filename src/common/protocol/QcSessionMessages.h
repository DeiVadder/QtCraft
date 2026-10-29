// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Protocol messages of the connection and session: handshake, content, chat and world time.
// Part of the QcMessage variant (QcMessages.h).

#include "common/content/mobs/QcAppearance.h"
#include "common/content/registry/QcContentRegistry.h"

#include <QString>
#include <QStringList>

#include <memory>

namespace QcMsg {

// Client -> server: first message on every connection.
struct Hello
{
    // Longer names are refused by the decoder before the server cleans them (32 code points).
    static constexpr qsizetype maxNameLength = 256;
    static constexpr qsizetype maxClientLength = 256;

    quint32 protocolVersion = 0;
    QString playerName;
    QcAppearance appearance;
    // Program, version, commit and system of the client (QcBuildInfo::clientText) for the server's
    // log (decision E-834); sent before the appearance.
    QString client;
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
    // The world's spawn column (x, z), where compasses point (decision E-769).
    qint32 spawnX = 0;
    qint32 spawnZ = 0;
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
    static constexpr qsizetype maxContentKey = 256;

    QString text;
    QString key;
    QStringList arguments;
    // Per argument the content translation key it stands for (decision E-873; the kind of the
    // creature that killed a player): the client shows the key's text in its content language and
    // the argument (English) where it knows none. Empty, or as long as `arguments` (empty entries
    // for plain arguments). An entry with QcChatText::messageKeyPrefix names a server message
    // instead, which the client translates (decision E-911).
    QStringList contentKeys = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisation warning-free
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

// Server -> client: measures the round trip for the admin channel's player list (decision E-848);
// the client's tick thread answers at once with a Pong of the same nonce.
struct Ping
{
    quint32 nonce = 0;
};

// Client -> server: the answer to Ping.
struct Pong
{
    quint32 nonce = 0;
};

// Client -> server: the player opened (`paused`) or left the pause menu (decision E-877). Only the
// local player of a world hosted in the game pauses it, and only while no one else is connected; the
// server ignores the wish of a network client.
struct PauseRequest
{
    bool paused = false;
};

// Server -> client: the server stopped (`paused`) or resumed its ticks (decision E-877). While paused
// the client predicts no movement of its own and sends no input; the connection stays.
struct ServerPause
{
    bool paused = false;
};

// Client -> server: measures the round trip of the connection (decision E-886). The client's tick
// thread sends one every half second while it plays; the server answers on its thread at once with a
// LatencyEcho of the same nonce, without waiting for a tick. `reportedUs` is the client's smoothed
// round trip so far, which the server lists with the player (PlayerList); -1 while unknown.
struct LatencyProbe
{
    // Longer round trips are reported as this (a minute).
    static constexpr qint32 maxReportedUs = 60'000'000;

    quint32 nonce = 0;
    qint32 reportedUs = -1;
};

// Server -> client: the answer to LatencyProbe.
struct LatencyEcho
{
    quint32 nonce = 0;
};

} // namespace QcMsg
