#include "QcTradeController.h"

#include "QcClientSession.h"
#include "QcContentRegistry.h"
#include "QcSessionParts.h"
#include "QcSettingsController.h"

#include <algorithm>

using namespace Qt::StringLiterals;

QcTradeController::QcTradeController(const QcSessionParts &parts, const QcSettingsController &settings, QObject *parent)
    : QObject(parent)
    , m_parts(parts)
    , m_settings(settings)
{
}

bool QcTradeController::isOpen() const
{
    return m_parts.session && m_parts.session->trading().isOpen();
}

QString QcTradeController::profession() const
{
    if (!isOpen() || !m_parts.session->content()) {
        return {};
    }
    return m_parts.session->content()->translate(m_parts.session->trading().state().profession, m_settings.contentLanguage());
}

int QcTradeController::level() const
{
    return isOpen() ? m_parts.session->trading().state().level : 0;
}

QString QcTradeController::levelName() const
{
    return isOpen() ? nameOfLevel(level()) : QString();
}

QString QcTradeController::nameOfLevel(int level)
{
    switch (level) {
    case 1:
        return tr("Novice");
    case 2:
        return tr("Apprentice");
    case 3:
        return tr("Journeyman");
    case 4:
        return tr("Expert");
    default:
        return tr("Master");
    }
}

double QcTradeController::levelProgress() const
{
    if (!isOpen()) {
        return 0.0;
    }
    const QcMsg::TradeState &state = m_parts.session->trading().state();
    if (state.levelEnd <= state.levelStart) {
        return 1.0;
    }
    return std::clamp(double(state.experience - state.levelStart) / double(state.levelEnd - state.levelStart), 0.0, 1.0);
}

void QcTradeController::select(int index)
{
    const int clamped = std::clamp(index, 0, std::max(0, int(m_offers.size()) - 1));
    if (clamped != m_selected) {
        m_selected = clamped;
        emit changed();
    }
}

void QcTradeController::trade(int index, bool repeat) const
{
    if (isOpen()) {
        m_parts.session->trading().trade(index, repeat);
    }
}

QVariantMap QcTradeController::stackMap(QcItemStack stack) const
{
    const std::shared_ptr<const QcContentRegistry> content = m_parts.session->content();
    if (!content || !content->isValidItem(stack.item)) {
        return {{u"icon"_s, QString()}, {u"count"_s, 0}, {u"label"_s, QString()}};
    }
    const QcItemDefinition &item = content->item(stack.item);
    return {{u"icon"_s, QString(u"image://items/"_s + item.name)},
            {u"count"_s, stack.count},
            {u"label"_s, content->translate(item.translationKey, m_settings.contentLanguage())}};
}

void QcTradeController::update()
{
    m_offers.clear();
    if (isOpen()) {
        const QcClientTrading &trading = m_parts.session->trading();
        const QList<QcItemStack> &inventory = m_parts.session->inventory().allSlots();
        for (qsizetype i = 0; i < trading.state().offers.size(); ++i) {
            const QcMsg::TradeOffer &offer = trading.state().offers[i];
            QVariantList costs;
            for (const QcItemStack &cost : offer.costs) {
                costs.append(stackMap(cost));
            }
            m_offers.append(QVariantMap{{u"costs"_s, costs},
                                        {u"result"_s, stackMap(offer.result)},
                                        {u"uses"_s, offer.uses},
                                        {u"maxUses"_s, offer.maxUses},
                                        {u"soldOut"_s, offer.uses >= offer.maxUses},
                                        {u"affordable"_s, trading.affordable(int(i), inventory)}});
        }
    }
    m_selected = std::clamp(m_selected, 0, std::max(0, int(m_offers.size()) - 1));
    emit changed();
}

void QcTradeController::reset()
{
    m_selected = 0;
    update();
}
