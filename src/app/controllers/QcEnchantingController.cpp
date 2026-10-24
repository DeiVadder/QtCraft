#include "app/controllers/QcEnchantingController.h"

#include "app/controllers/QcSettingsController.h"
#include "app/debug/QcAppLogging.h"
#include "app/models/QcEnchantmentTexts.h"
#include "app/session/QcSessionParts.h"
#include "clientcore/session/QcClientSession.h"
#include "clientcore/session/QcRuneScript.h"
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

namespace {

// The signs of a clue for QML: one map per character with its rune (-1: readable) and text.
QVariantList signsOf(const std::vector<QcRuneScript::Sign> &signs)
{
    QVariantList list;
    list.reserve(static_cast<qsizetype>(signs.size()));
    for (const QcRuneScript::Sign &sign : signs) {
        list.append(QVariantMap{{u"rune"_s, sign.rune}, {u"text"_s, sign.character}});
    }
    return list;
}

} // namespace

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
            const QcMsg::EnchantingOffer &offer = state.offers[static_cast<std::size_t>(slot)];
            const int price = QcEnchantingTable::priceOf(slot);
            const QString clue = offer.levelCost > 0
                                     ? QcEnchantmentTexts::nameOf(*content, {offer.clue, offer.clueLevel}, m_settings.contentLanguage())
                                     : QString();
            // Why an offer cannot be taken, for screen readers.
            const bool levelsLacking = !free && level < offer.levelCost;
            const bool lapisLacking = !free && lapis < price;
            const bool affordable = offer.levelCost > 0 && !levelsLacking && !lapisLacking;
            const std::vector<QcRuneScript::Sign> signs = QcRuneScript::write(clue, state.runeSeed, slot, state.shelves);
            const QcRuneScript::Legibility legible = QcRuneScript::legibility(signs);
            m_offers.append(QVariantMap{{u"levelCost"_s, offer.levelCost},
                                        {u"price"_s, price},
                                        {u"signs"_s, signsOf(signs)},
                                        {u"letters"_s, legible.letters},
                                        {u"readableLetters"_s, legible.readable},
                                        // The clue in plain text only once every letter is readable.
                                        {u"clue"_s, legible.readable == legible.letters ? clue : QString()},
                                        {u"levelsLacking"_s, levelsLacking},
                                        {u"lapisLacking"_s, lapisLacking},
                                        {u"affordable"_s, affordable}});
        }
    }
    emit changed();
}
