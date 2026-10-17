#include "server/core/QcRateLimiter.h"

#include <algorithm>

std::optional<QcRateLimiter::Kind> QcRateLimiter::kindOf(const QcMessage &message)
{
    // An explicit list; test_server_limits checks that every client message either has a bucket or
    // is one of the deliberately unlimited ones (isUnlimited).
    if (std::holds_alternative<QcMsg::StartMining>(message) || std::holds_alternative<QcMsg::CancelMining>(message)
        || std::holds_alternative<QcMsg::FinishMining>(message)) {
        return Kind::Mining;
    }

    if (std::holds_alternative<QcMsg::UseItem>(message) || std::holds_alternative<QcMsg::UseEntity>(message)
        || std::holds_alternative<QcMsg::ConsumeItem>(message)) {
        return Kind::Use;
    }

    if (std::holds_alternative<QcMsg::AttackEntity>(message)) {
        return Kind::AttackEntity;
    }
    if (std::holds_alternative<QcMsg::ChatInput>(message)) {
        return Kind::Chat;
    }
    if (std::holds_alternative<QcMsg::DropItem>(message)) {
        return Kind::Drop;
    }
    if (std::holds_alternative<QcMsg::SetAppearance>(message)) {
        return Kind::Appearance;
    }

    const bool inventory = std::holds_alternative<QcMsg::SelectHotbarSlot>(message)
                           || std::holds_alternative<QcMsg::MoveInventoryItem>(message)
                           || std::holds_alternative<QcMsg::QuickMoveItem>(message)
                           || std::holds_alternative<QcMsg::TakeCraftingResult>(message)
                           || std::holds_alternative<QcMsg::CraftRecipe>(message)
                           || std::holds_alternative<QcMsg::CloseCrafting>(message)
                           || std::holds_alternative<QcMsg::CloseMachine>(message)
                           || std::holds_alternative<QcMsg::CreativeItem>(message)
                           || std::holds_alternative<QcMsg::PickBlock>(message)
                           || std::holds_alternative<QcMsg::Trade>(message)
                           || std::holds_alternative<QcMsg::CloseTrade>(message);
    return inventory ? std::optional<Kind>(Kind::Inventory) : std::nullopt;
}

bool QcRateLimiter::isUnlimited(const QcMessage &message)
{
    return std::holds_alternative<QcMsg::Hello>(message) || std::holds_alternative<QcMsg::PlayerInput>(message)
           || std::holds_alternative<QcMsg::Respawn>(message) || std::holds_alternative<QcMsg::Disconnect>(message)
           || std::holds_alternative<QcMsg::DebugSubscribe>(message) || std::holds_alternative<QcMsg::LoginResponse>(message);
}

const char *QcRateLimiter::name(Kind kind)
{
    static constexpr std::array<const char *, kindCount> names{"mining", "use", "attack", "chat", "drop", "inventory", "appearance", "switch"};
    return names[size_t(kind)];
}

bool QcRateLimiter::allow(Kind kind, qint64 nowNs)
{
    const Limit rule = limit(kind);
    Bucket &bucket = m_buckets[size_t(kind)];

    // A bucket starts full on its first message.
    if (bucket.tokens < 0.0) {
        bucket.tokens = rule.burst;
        bucket.refilledAt = nowNs;
    }

    if (nowNs > bucket.refilledAt) {
        const double refill = double(nowNs - bucket.refilledAt) * 1e-9 * rule.perSecond;
        bucket.tokens = std::min(rule.burst, bucket.tokens + refill);
        bucket.refilledAt = nowNs;
    }

    if (bucket.tokens < 1.0) {
        ++bucket.dropped;
        return false;
    }

    bucket.tokens -= 1.0;
    return true;
}

quint64 QcRateLimiter::takeDropReport(Kind kind, qint64 nowNs)
{
    Bucket &bucket = m_buckets[size_t(kind)];
    if (bucket.dropped == 0 || (bucket.reported && nowNs - bucket.reportedAt < reportIntervalNs)) {
        return 0;
    }

    const quint64 dropped = bucket.dropped;
    bucket.dropped = 0;
    bucket.reported = true;
    bucket.reportedAt = nowNs;
    return dropped;
}
