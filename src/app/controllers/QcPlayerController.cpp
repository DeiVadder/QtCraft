#include "QcPlayerController.h"

#include "QcArmour.h"
#include "QcClientSession.h"
#include "QcCrafting.h"
#include "QcSessionParts.h"
#include "QcSettingsController.h"

#include <QVariantMap>

using namespace Qt::StringLiterals;

QcPlayerController::QcPlayerController(const QcSessionParts &parts, const QcSettingsController &settings, QObject *parent)
    : QObject(parent)
    , m_parts(parts)
    , m_settings(settings)
    , m_inventory(new QcInventoryModel(this))
    , m_creativeItems(new QcCreativeItemModel(this))
{
}

bool QcPlayerController::creative() const
{
    return m_parts.session && m_parts.session->gameMode().rules().itemCatalogue;
}

void QcPlayerController::putCreativeItem(int row, const QString &itemName, int count) const
{
    const auto content = m_parts.session ? m_parts.session->content() : nullptr;
    const std::optional<QcItemId> item = content ? content->itemId(itemName) : std::nullopt;
    const int slot = row < 0 ? QcMsg::CreativeItem::addSlot : QcInventoryModel::slotOfRow(row);
    const bool inventoryOrArmour = slot == QcMsg::CreativeItem::addSlot || QcInventory::isValidSlot(slot)
                                   || QcMsg::MoveInventoryItem::isEquipmentSlot(slot);
    if (item && count > 0 && inventoryOrArmour) {
        m_parts.session->gameMode().creativeItem(slot, {*item, count, 0});
    }
}

void QcPlayerController::clearSlot(int row) const
{
    const int slot = QcInventoryModel::slotOfRow(row);
    if (m_parts.session && (QcInventory::isValidSlot(slot) || QcMsg::MoveInventoryItem::isEquipmentSlot(slot))) {
        m_parts.session->gameMode().creativeItem(slot, {});
    }
}

double QcPlayerController::health() const
{
    return m_parts.session ? m_parts.session->vitals().health() : 0.0;
}

double QcPlayerController::maxHealth() const
{
    return m_parts.session ? m_parts.session->vitals().maxHealth() : 20.0;
}

bool QcPlayerController::dead() const
{
    return m_parts.session && m_parts.session->isDead();
}

bool QcPlayerController::burning() const
{
    return m_parts.session && m_parts.session->vitals().burning();
}

int QcPlayerController::food() const
{
    return m_parts.session ? m_parts.session->vitals().food() : QcItemDefinition::maxFood;
}

int QcPlayerController::maxFood() const
{
    return m_parts.session ? m_parts.session->vitals().maxFood() : QcItemDefinition::maxFood;
}

int QcPlayerController::experienceLevel() const
{
    return m_parts.session ? m_parts.session->vitals().experienceLevel() : 0;
}

double QcPlayerController::experienceProgress() const
{
    return m_parts.session ? m_parts.session->vitals().experienceProgress() : 0.0;
}

double QcPlayerController::machineBurn() const
{
    return m_parts.session ? m_parts.session->machineBurn() : 0.0;
}

int QcPlayerController::containerSlots() const
{
    return m_parts.session && m_parts.session->containerOpen() ? int(m_parts.session->machineSlots().size()) : 0;
}

double QcPlayerController::machineProgress() const
{
    return m_parts.session ? m_parts.session->machineProgress() : 0.0;
}

int QcPlayerController::craftingSize() const
{
    return m_parts.session ? m_parts.session->craftingSize() : 2;
}

void QcPlayerController::selectHotbarSlot(int slot) const
{
    if (m_parts.session) {
        m_parts.session->selectHotbarSlot(slot);
    }
}

void QcPlayerController::moveItem(int from, int to, int count) const
{
    const int fromSlot = QcInventoryModel::slotOfRow(from);
    const int toSlot = QcInventoryModel::slotOfRow(to);
    if (m_parts.session && fromSlot >= 0 && toSlot >= 0) {
        m_parts.session->moveInventoryItem(fromSlot, toSlot, count);
    }
}

void QcPlayerController::quickMove(int row) const
{
    if (!m_parts.session) {
        return;
    }
    if (row == QcInventoryModel::resultRow) {
        m_parts.session->quickMoveItem(QcMsg::QuickMoveItem::craftingResultSlot);
    } else if (const int slot = QcInventoryModel::slotOfRow(row); slot >= 0) {
        m_parts.session->quickMoveItem(slot);
    }
}

void QcPlayerController::takeCraftingResult() const
{
    if (m_parts.session) {
        m_parts.session->takeCraftingResult();
    }
}

void QcPlayerController::craftRecipe(const QString &recipe) const
{
    if (m_parts.session) {
        m_parts.session->craftRecipe(recipe);
    }
}

void QcPlayerController::respawn() const
{
    if (m_parts.session) {
        m_parts.session->respawn();
    }
}

int QcPlayerController::armour() const
{
    const auto content = m_parts.session ? m_parts.session->content() : nullptr;
    return content ? QcArmour::points(*content, m_parts.session->equipment()) : 0;
}

int QcPlayerController::maxArmour()
{
    return QcArmour::maxEffectivePoints;
}

QStringList QcPlayerController::armourPlaceholders() const
{
    const auto content = m_parts.session ? m_parts.session->content() : nullptr;
    QStringList names;
    for (const QcEquipmentSlot slot : {QcEquipmentSlot::Head, QcEquipmentSlot::Chest, QcEquipmentSlot::Legs, QcEquipmentSlot::Feet}) {
        const std::optional<QcItemId> item = content ? QcArmour::placeholder(*content, slot) : std::nullopt;
        names.append(item ? content->item(*item).name : QString());
    }
    return names;
}

QString QcPlayerController::armourIcon() const
{
    const auto content = m_parts.session ? m_parts.session->content() : nullptr;
    const std::optional<QcItemId> item = content ? QcArmour::pointsIcon(*content) : std::nullopt;
    return item ? content->item(*item).name : QString();
}

void QcPlayerController::useContent()
{
    m_inventory->setContent(m_parts.session->content(), m_settings.contentLanguage());
    m_creativeItems->setContent(m_parts.session->content(), m_settings.contentLanguage());
    emit contentChanged();
    updateInventory();
}

void QcPlayerController::updateInventory()
{
    const QcClientSession &session = *m_parts.session;
    m_inventory->setInventory(session.inventory(), session.craftingGrid(), session.craftingResult(),
                              session.machineOpen() ? session.machineSlots() : QList<QcItemStack>(), session.equipment());
    updateCraftableRecipes();
    emit craftingChanged();
}

void QcPlayerController::updateCraftableRecipes()
{
    // Recipes the inventory can make right now, for the recipe list next to the grid.
    m_craftableRecipes.clear();
    const QcClientSession &session = *m_parts.session;
    const auto content = session.content();
    if (!content) {
        return;
    }

    for (const QcRecipeDefinition &recipe : content->recipes()) {
        if (QcCrafting::gridSizeNeeded(recipe) > session.craftingSize()) {
            continue;
        }
        const int possible = QcCrafting::timesCraftable(*content, session.inventory(), recipe, 64);
        const std::optional<QcItemId> result = content->itemId(recipe.resultItem);
        if (possible <= 0 || !result) {
            continue;
        }

        const QcItemDefinition &definition = content->item(*result);
        m_craftableRecipes.append(QVariantMap{{u"recipe"_s, recipe.name},
                                              {u"label"_s, content->translate(definition.translationKey, m_settings.contentLanguage())},
                                              {u"icon"_s, QString(u"image://items/"_s + definition.name)},
                                              {u"count"_s, recipe.resultCount},
                                              {u"possible"_s, possible}});
    }
}

void QcPlayerController::nameOpenMachine()
{
    const QcClientSession &session = *m_parts.session;
    const auto content = session.content();
    const std::optional<QcBlockPos> pos = session.machinePos();
    if (session.machineEntity() != 0) {
        // A container cart: named after its item, its rows as long as those of the block it carries.
        const QcClientEntity cart = session.entities().value(session.machineEntity());
        const std::optional<QcBlockId> carried = cart.cartBlock(*content);
        m_machineLabel = content->isValidItem(cart.item.item) ? content->translate(content->item(cart.item.item).translationKey, m_settings.contentLanguage()) : QString();
        m_containerColumns = carried ? content->block(*carried).containerColumns : 9;
        return;
    }
    const std::optional<QcBlockId> block = pos && session.world() ? session.world()->blockAt(*pos) : std::nullopt;
    m_machineLabel = block ? content->translate(content->block(*block).translationKey, m_settings.contentLanguage()) : QString();
    m_containerColumns = block ? content->block(*block).containerColumns : 9;
}

void QcPlayerController::updateMachine()
{
    updateInventory();
    emit machineChanged();
}
