#pragma once

#include "QcMessages.h"

#include <QtGlobal>

#include <array>
#include <optional>

// Token buckets of one player, one per kind of spammable message (decision E-091). A bucket starts
// full, refills with the server ticks (so it follows game time, also when ticks are caught up) and
// pays one token per message; a message finding its bucket empty is dropped. The limits lie well
// above what the real client sends while playing.
class QcRateLimiter
{
public:
    enum class Kind : quint8 {
        // StartMining, CancelMining, FinishMining (the client sends at most three per tick, 60 per
        // second; the limit leaves a clear margin, a dropped StartMining or FinishMining gets its
        // true block back).
        Mining,
        // UseItem, UseEntity, ConsumeItem (one per click).
        Use,
        AttackEntity,
        // Chat lines and commands.
        Chat,
        // Throwing items (each spawns an entity).
        Drop,
        // Inventory, crafting and machine screen actions; mouse drags and the wheel send many.
        Inventory,
        // A new look is sent again to every player who sees the player.
        Appearance,
    };
    static constexpr int kindCount = 7;

    struct Limit
    {
        double perSecond = 0.0;
        double burst = 0.0;
    };
    static constexpr std::array<Limit, kindCount> limits{{
        {100.0, 100.0},
        {10.0, 20.0},
        {10.0, 20.0},
        {2.0, 20.0},
        {10.0, 20.0},
        {40.0, 80.0},
        {2.0, 10.0},
    }};
    // Drops are logged at most this often per player and kind (the first one at once).
    static constexpr quint64 reportIntervalTicks = 20;

    static constexpr Limit limit(Kind kind) { return limits[size_t(kind)]; }
    // The bucket a client message pays into; std::nullopt for messages that are not limited here.
    static std::optional<Kind> kindOf(const QcMessage &message);
    // The client messages that are deliberately not limited here: PlayerInput has its own budget,
    // Hello, Respawn and Disconnect are harmless, DebugSubscribe is state (the latest one counts and
    // must never be dropped; the debug data itself comes at a fixed pace, decision E-225). Every
    // other client message needs a bucket.
    static bool isUnlimited(const QcMessage &message);
    static const char *name(Kind kind);

    // Takes a token at server tick `tick`; false when the bucket is empty (the drop is counted).
    bool allow(Kind kind, quint64 tick);
    // Drops of `kind` not yet reported, when a report is due at `tick` (then they count as
    // reported); 0 otherwise.
    quint64 takeDropReport(Kind kind, quint64 tick);

private:
    struct Bucket
    {
        double tokens = -1.0;
        quint64 refilledAt = 0;
        quint64 dropped = 0;
        bool reported = false;
        quint64 reportedAt = 0;
    };

    std::array<Bucket, kindCount> m_buckets;
};
