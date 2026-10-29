// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

// Payloads of the trading messages (QcTradeMessages.h, decision E-414).

#include "common/protocol/QcProtocolCodec.h"

#include "common/protocol/QcStreamLimits.h"

using namespace Qt::StringLiterals;

namespace {

constexpr qsizetype maxProfessionKey = 128;

void writeOffer(QDataStream &stream, const QcMsg::TradeOffer &offer)
{
    QcProtocolCodec::writeStacks(stream, offer.costs);
    QcProtocolCodec::writeStack(stream, offer.result);
    stream << offer.uses << offer.maxUses;
}

// std::nullopt for an offer without costs or with more than TradeOffer::maxCosts, or with uses out of range.
[[nodiscard]] std::optional<QcMsg::TradeOffer> readOffer(QDataStream &stream)
{
    std::optional<QList<QcItemStack>> costs = QcProtocolCodec::readStacks(stream);
    if (!costs || costs->isEmpty() || costs->size() > QcMsg::TradeOffer::maxCosts) {
        return std::nullopt;
    }

    QcMsg::TradeOffer offer;
    offer.costs = std::move(*costs);
    offer.result = QcProtocolCodec::readStack(stream);
    stream >> offer.uses >> offer.maxUses;
    const bool valid = offer.maxUses >= 0 && offer.uses >= 0 && offer.uses <= offer.maxUses;
    return valid ? std::optional(std::move(offer)) : std::nullopt;
}

} // namespace

namespace QcProtocolCodec {

void encode(QDataStream &stream, const QcMsg::TradeState &m)
{
    stream << m.villager << m.profession << m.level << m.experience << m.levelStart << m.levelEnd;
    stream << static_cast<quint32>(m.offers.size());
    for (const QcMsg::TradeOffer &offer : m.offers) {
        writeOffer(stream, offer);
    }
}

void encode(QDataStream &stream, QcMsg::Trade m)
{
    stream << m.villager << m.offer << m.repeat;
}

void encode(QDataStream &, QcMsg::CloseTrade) {}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::TradeState>, QDataStream &stream)
{
    QcMsg::TradeState m;
    stream >> m.villager >> m.profession >> m.level >> m.experience >> m.levelStart >> m.levelEnd;
    const std::optional<quint32> count =
        m.profession.size() > maxProfessionKey ? std::nullopt : QcStreamLimits::readCount(stream, QcMsg::TradeState::maxOffers);
    if (!count) {
        return qcError(u"Corrupt trading screen"_s);
    }

    for (quint32 i = 0; i < *count; ++i) {
        std::optional<QcMsg::TradeOffer> offer = readOffer(stream);
        if (!offer) {
            return qcError(u"Corrupt trade offer"_s);
        }
        m.offers.append(std::move(*offer));
    }
    if (m.level < 1 || m.experience < 0 || m.levelStart < 0 || m.levelEnd < m.levelStart) {
        return qcError(u"Invalid villager level"_s);
    }
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::Trade>, QDataStream &stream)
{
    QcMsg::Trade m;
    stream >> m.villager >> m.offer >> m.repeat;
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::CloseTrade>, QDataStream &)
{
    return QcMsg::CloseTrade{};
}

} // namespace QcProtocolCodec
