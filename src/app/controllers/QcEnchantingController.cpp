#include "app/controllers/QcEnchantingController.h"

#include "app/controllers/QcSettingsController.h"
#include "app/debug/QcAppLogging.h"
#include "app/models/QcEnchantmentTexts.h"
#include "app/session/QcSessionParts.h"
#include "clientcore/session/QcClientSession.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/gameplay/QcEnchantingTable.h"

#include <utility>

using namespace Qt::StringLiterals;

QcEnchantingController::QcEnchantingController(const QcSessionParts &parts, const QcSettingsController &settings, QObject *parent)
    : QObject(parent)
    , m_parts(parts)
    , m_settings(settings)
{
}

bool QcEnchantingController::isOpen() const
{
    return m_parts.session && m_parts.session->enchanting().isOpen();
}

void QcEnchantingController::choose(int index) const
{
    if (!isOpen() || index < 0 || index >= QcEnchantingTable::offerCount) {
        qCWarning(lcGame) << "Enchanting: no offer" << index;
        return;
    }
    m_parts.session->enchanting().choose(index);
}

void QcEnchantingController::update()
{
    m_offers.clear();
    m_shelves = 0;
    const std::shared_ptr<const QcContentRegistry> content = isOpen() ? m_parts.session->content() : nullptr;
    if (content) {
        const QcMsg::EnchantingState &state = m_parts.session->enchanting().state();
        const bool free = m_parts.session->gameMode().rules().itemCatalogue;
        const int level = m_parts.session->vitals().experienceLevel();
        const int lapis = state.lapis.isEmpty() ? 0 : state.lapis.count;
        m_shelves = state.shelves;
        for (int slot = 0; std::cmp_less(slot, state.offers.size()); ++slot) {
            const QcMsg::EnchantingOffer &offer = state.offers[std::size_t(slot)];
            const int price = QcEnchantingTable::priceOf(slot);
            const QString clue = offer.levelCost > 0
                                     ? QcEnchantmentTexts::nameOf(*content, {offer.clue, offer.clueLevel}, m_settings.contentLanguage())
                                     : QString();
            const bool affordable = offer.levelCost > 0 && (free || (level >= offer.levelCost && lapis >= price));
            m_offers.append(QVariantMap{
                {u"levelCost"_s, offer.levelCost}, {u"price"_s, price}, {u"clue"_s, clue}, {u"affordable"_s, affordable}});
        }
    }
    emit changed();
}
