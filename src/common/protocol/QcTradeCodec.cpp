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

// False for an offer without costs or with more than TradeOffer::maxCosts, or with uses out of range.
bool readOffer(QDataStream &stream, QcMsg::TradeOffer &offer)
{
    if (!QcProtocolCodec::readStacks(stream, offer.costs) || offer.costs.isEmpty() || offer.costs.size() > QcMsg::TradeOffer::maxCosts) {
        return false;
    }
    offer.result = QcProtocolCodec::readStack(stream);
    stream >> offer.uses >> offer.maxUses;
    return offer.maxUses >= 0 && offer.uses >= 0 && offer.uses <= offer.maxUses;
}

} // namespace

namespace QcProtocolCodec {

void encode(QDataStream &stream, const QcMsg::TradeState &m)
{
    stream << m.villager << m.profession << m.level << m.experience << m.levelStart << m.levelEnd;
    stream << quint32(m.offers.size());
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
    quint32 count = 0;
    if (m.profession.size() > maxProfessionKey || !QcStreamLimits::readCount(stream, QcMsg::TradeState::maxOffers, count)) {
        return qcError(u"Corrupt trading screen"_s);
    }

    for (quint32 i = 0; i < count; ++i) {
        QcMsg::TradeOffer offer;
        if (!readOffer(stream, offer)) {
            return qcError(u"Corrupt trade offer"_s);
        }
        m.offers.append(offer);
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
