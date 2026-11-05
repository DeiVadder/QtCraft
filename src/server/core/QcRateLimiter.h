// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/protocol/QcMessages.h"

#include <QtGlobal>

#include <array>
#include <optional>

/*! \brief Token buckets of one player, one per kind of spammable message (decision E-091).

    A bucket starts full, refills with the players' time of the server
    (QcServerClock::playerTimeNs: real time at every tick rate and during a sprint, deterministic
    in tests) and pays one token per message; a message finding its bucket empty is dropped. The
    limits lie well above what the real client sends while playing. */
class QcRateLimiter
{
public:
    /*! The kinds of spammable messages, one bucket each. */
    enum class Kind : quint8 {
        /*! StartMining, CancelMining, FinishMining (the client sends at most three per tick, 60
            per second; the limit leaves a clear margin, a dropped StartMining or FinishMining
            gets its true block back). */
        Mining,
        /*! UseItem, UseEntity, ConsumeItem, ThrowItem, the press of DrawLauncher (one per
            click). */
        Use,
        /*! AttackEntity (one per click). */
        AttackEntity,
        /*! Chat lines and commands. */
        Chat,
        /*! DropItem: dropping items from the hotbar (each spawns an item entity). */
        Drop,
        /*! Inventory, crafting and machine screen actions; mouse drags and the wheel send many.
            The pause menu's PauseRequest counts here too (decision E-877). */
        Inventory,
        /*! A new look is sent again to every player who sees the player. */
        Appearance,
        /*! Levers, buttons and repeaters switched by hand (paid inside a Use, decision E-339):
            every switch starts work in the circuits, so spamming a lever is bounded separately. */
        Switch,
        /*! Lines written on a sign (decision E-766); each change goes to every player who has
            the sign's chunk. */
        Sign,
        /*! Admin requests and subscriptions of operators (decision E-848): each may run a
            command or build a report of the whole server. */
        Admin,
        /*! Latency probes (decision E-886): the client sends two per second; each costs an echo. */
        Latency,
    };
    /*! The number of kinds. */
    static constexpr int kindCount = 11;

    /*! \brief The limit of a bucket: tokens refilled per second and the bucket's size.
        \qcfields */
    struct Limit
    {
        double perSecond = 0.0;
        double burst = 0.0;
    };
    /*! The limits by kind, in the order of Kind. */
    static constexpr std::array<Limit, kindCount> limits{{
        {100.0, 100.0},
        {10.0, 20.0},
        {10.0, 20.0},
        {2.0, 20.0},
        {10.0, 20.0},
        {40.0, 80.0},
        {2.0, 10.0},
        {5.0, 10.0},
        {1.0, 4.0},
        {5.0, 20.0},
        {4.0, 4.0},
    }};
    /*! Drops are logged at most this often per player and kind (the first one at once). */
    static constexpr qint64 reportIntervalNs = 1'000'000'000;

    /*! Returns the limit of \a kind. */
    static constexpr Limit limit(Kind kind) { return limits[static_cast<size_t>(kind)]; }
    /*! Returns the bucket the client message \a message pays into; std::nullopt for messages
        that are not limited here. */
    [[nodiscard]] static std::optional<Kind> kindOf(const QcMessage &message);
    /*! Returns whether \a message is one of the client messages that are deliberately not
        limited here: PlayerInput has its own budget, Hello, Respawn, Disconnect and the release
        or cancel of DrawLauncher are harmless, LoginResponse is accepted once before the client
        plays (the connection limits of QcNetworkServer bound logins), Pong only answers the
        server's own Ping, DebugSubscribe is state (the latest one counts and must never be
        dropped; the debug data itself comes at a fixed pace, decision E-225). Every other client
        message needs a bucket. */
    static bool isUnlimited(const QcMessage &message);
    /*! Returns the name of \a kind as the log shows it ("mining", "use", ...). */
    static const char *name(Kind kind);

    /*! Takes a token of \a kind at \a nowNs (players' time). Returns \c false when the bucket is
        empty (the drop is counted). */
    bool allow(Kind kind, qint64 nowNs);
    /*! Returns the drops of \a kind not yet reported, when a report is due at \a nowNs (then
        they count as reported); 0 otherwise. */
    quint64 takeDropReport(Kind kind, qint64 nowNs);

private:
    struct Bucket
    {
        double tokens = -1.0;
        qint64 refilledAt = 0;
        quint64 dropped = 0;
        bool reported = false;
        qint64 reportedAt = 0;
    };

    std::array<Bucket, kindCount> m_buckets;
};
