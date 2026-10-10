#include "QcPlayerController.h"

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
{
}

double QcPlayerController::health() const
{
    return m_parts.session ? m_parts.session->health() : 0.0;
}

double QcPlayerController::maxHealth() const
{
    return m_parts.session ? m_parts.session->maxHealth() : 20.0;
}

bool QcPlayerController::dead() const
{
    return m_parts.session && m_parts.session->isDead();
}

int QcPlayerController::food() const
{
    return m_parts.session ? m_parts.session->food() : QcItemDefinition::maxFood;
}

int QcPlayerController::maxFood() const
{
    return m_parts.session ? m_parts.session->maxFood() : QcItemDefinition::maxFood;
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

void QcPlayerController::useContent()
{
    m_inventory->setContent(m_parts.session->content(), m_settings.contentLanguage());
    updateInventory();
}

void QcPlayerController::updateInventory()
{
    const QcClientSession &session = *m_parts.session;
    m_inventory->setInventory(session.inventory(), session.craftingGrid(), session.craftingResult(),
                              session.machineOpen() ? session.machineSlots() : QList<QcItemStack>());
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
    const std::optional<QcBlockId> block = pos && session.world() ? session.world()->blockAt(*pos) : std::nullopt;
    m_machineLabel = block ? content->translate(content->block(*block).translationKey, m_settings.contentLanguage()) : QString();
}

void QcPlayerController::updateMachine()
{
    updateInventory();
    emit machineChanged();
}
