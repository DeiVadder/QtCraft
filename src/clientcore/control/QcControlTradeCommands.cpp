#include "clientcore/control/QcControlTradeCommands.h"

#include "clientcore/control/QcControlAim.h"
#include "clientcore/control/QcControlArguments.h"
#include "clientcore/control/QcControlCommandTable.h"
#include "clientcore/control/QcControlContext.h"
#include "clientcore/session/QcClientSession.h"
#include "common/content/registry/QcContentRegistry.h"

#include <QJsonArray>

using namespace Qt::StringLiterals;

QcControlTradeCommands::QcControlTradeCommands(QcControlContext &context)
    : m_context(context)
{
}

void QcControlTradeCommands::registerCommands(QcControlCommandTable &table)
{
    table.add(u"trades"_s, [this](const QJsonObject &request) { trades(request); });
    table.add(u"openTrade"_s, [this](const QJsonObject &request) { openTrade(request); });
    table.add(u"trade"_s, [this](const QJsonObject &request) { trade(request); });
    table.add(u"closeTrade"_s, [this](const QJsonObject &request) { closeTrade(request); });
}

QcClientSession &QcControlTradeCommands::session() const
{
    return *m_context.session();
}

QJsonObject QcControlTradeCommands::tradesJson() const
{
    const QcClientTrading &trading = session().trading();
    if (!trading.isOpen()) {
        return {{u"open"_s, false}};
    }

    const QcMsg::TradeState &state = trading.state();
    QJsonArray offers;
    for (qsizetype i = 0; i < state.offers.size(); ++i) {
        const QcMsg::TradeOffer &offer = state.offers[i];
        QJsonArray costs;
        for (const QcItemStack &cost : offer.costs) {
            costs.append(m_context.reports().stackJson(cost));
        }
        offers.append(QJsonObject{{u"index"_s, int(i)},
                                  {u"costs"_s, costs},
                                  {u"result"_s, m_context.reports().stackJson(offer.result)},
                                  {u"uses"_s, offer.uses},
                                  {u"maxUses"_s, offer.maxUses},
                                  {u"affordable"_s, trading.affordable(int(i), session().inventory().allSlots())}});
    }
    return {{u"open"_s, true},
            {u"villager"_s, qint64(state.villager)},
            {u"profession"_s, state.profession},
            {u"level"_s, state.level},
            {u"experience"_s, state.experience},
            {u"levelStart"_s, state.levelStart},
            {u"levelEnd"_s, state.levelEnd},
            {u"offers"_s, offers}};
}

void QcControlTradeCommands::trades(const QJsonObject &request)
{
    m_context.finish(request, tradesJson());
}

void QcControlTradeCommands::openTrade(const QJsonObject &request)
{
    // Looks at the villager and uses it like a player; answers once the server opened its screen.
    const quint64 startTick = session().tickCount();
    const quint32 wanted = quint32(request.value(u"entity"_s).toInteger(-1));
    const auto entity = session().entities().constFind(wanted);
    const std::shared_ptr<const QcContentRegistry> content = session().content();
    const QcMobDefinition *creature = content && entity != session().entities().constEnd() ? entity->creature(*content) : nullptr;
    if (!creature || !creature->villager) {
        m_context.fail(request, u"No villager with this id nearby"_s);
        return;
    }
    QcControlAim::lookAtPoint(session(), entity->position + QcVec3{0.0, creature->height * 0.5, 0.0});
    QcPlayerInput intent = session().intent();
    intent.sneak = false;
    session().setIntent(intent);

    bool used = false;
    constexpr int openTicks = 10;
    m_context.begin({request, QcControlActiveCommand::timeoutPolls(openTicks),
                     [this, startTick, used, wanted]() mutable {
                         if (session().trading().isOpen() && session().trading().state().villager == wanted) {
                             return true;
                         }
                         if (!used) {
                             if (session().tickCount() <= startTick) {
                                 return false;
                             }
                             if (session().hand().targetEntity() != std::optional<quint32>(wanted) || !session().useItem()) {
                                 m_context.setActiveError(u"The villager is not in reach or not in sight"_s);
                                 return true;
                             }
                             used = true;
                             return false;
                         }
                         if (session().tickCount() >= startTick + openTicks) {
                             m_context.setActiveError(u"The villager does not trade now (no profession, asleep, fleeing or busy)"_s);
                             return true;
                         }
                         return false;
                     },
                     [this] { return tradesJson(); }});
}

void QcControlTradeCommands::trade(const QJsonObject &request)
{
    const int offer = QcControlArguments::intField(request, u"offer"_s, -1);
    if (!session().trading().trade(offer, request.value(u"repeat"_s).toBool(false))) {
        m_context.fail(request, u"No trading screen is open or there is no such offer"_s);
        return;
    }
    m_context.waitTicks(request, session().tickCount(), 3, [this] { return tradesJson(); });
}

void QcControlTradeCommands::closeTrade(const QJsonObject &request)
{
    session().trading().close();
    m_context.finish(request, tradesJson());
}
