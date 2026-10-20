#include "app/controllers/QcStonecutterController.h"

#include "app/controllers/QcSettingsController.h"
#include "app/debug/QcAppLogging.h"
#include "app/models/QcItemIconUrls.h"
#include "app/session/QcSessionParts.h"
#include "clientcore/session/QcClientSession.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/content/registry/QcStonecutting.h"

#include <algorithm>

using namespace Qt::StringLiterals;

QcStonecutterController::QcStonecutterController(const QcSessionParts &parts, const QcSettingsController &settings, QObject *parent)
    : QObject(parent)
    , m_parts(parts)
    , m_settings(settings)
{
    // The lists carry icon URLs, which change with new icons (decision E-650).
    connect(QcItemIconUrls::instance(), &QcItemIconUrls::generationChanged, this, [this] {
        if (m_parts.session) {
            update();
        }
    });
}

bool QcStonecutterController::isOpen() const
{
    return m_parts.session && m_parts.session->stonecutter().isOpen();
}

void QcStonecutterController::select(int index)
{
    if (index < 0 || index >= m_inputNames.size()) {
        qCWarning(lcGame) << "Stonecutter: no input" << index << "of" << m_inputNames.size();
        return;
    }
    // The same input again changes nothing.
    if (index != m_selected) {
        m_selected = index;
        update();
    }
}

void QcStonecutterController::cut(int index, bool repeat) const
{
    if (!isOpen() || index < 0 || index >= m_recipes.size()) {
        qCWarning(lcGame) << "Stonecutter: no result" << index << "to cut";
        return;
    }
    m_parts.session->stonecutter().cut(m_recipes[index], repeat);
}

QVariantMap QcStonecutterController::stackMap(const QString &item, int count) const
{
    const std::shared_ptr<const QcContentRegistry> content = m_parts.session->content();
    const std::optional<QcItemId> id = content ? content->itemId(item) : std::nullopt;
    if (!id) {
        return {{u"icon"_s, QString()}, {u"count"_s, 0}, {u"label"_s, QString()}};
    }
    return {{u"icon"_s, QcItemIconUrls::url(item)},
            {u"count"_s, count},
            {u"label"_s, content->translations().translate(content->item(*id).translationKey, m_settings.contentLanguage())}};
}

void QcStonecutterController::update()
{
    const QString previous = m_inputNames.value(m_selected);
    m_inputNames.clear();
    m_recipes.clear();
    m_inputs.clear();
    m_results.clear();
    const std::shared_ptr<const QcContentRegistry> content = isOpen() ? m_parts.session->content() : nullptr;
    if (content) {
        const QcInventory &inventory = m_parts.session->inventory();
        m_inputNames = QcClientStonecutter::inputs(*content, inventory.allSlots());
        for (const QString &name : std::as_const(m_inputNames)) {
            m_inputs.append(stackMap(name, inventory.countOf(content->itemId(name).value_or(qcNoItem))));
        }
        m_selected = std::max(0, int(m_inputNames.indexOf(previous)));
        const QList<qsizetype> recipes = m_inputNames.isEmpty() ? QList<qsizetype>() : QcStonecutting::recipesFor(*content, m_inputNames[m_selected]);
        for (const qsizetype index : recipes) {
            const QcSmeltingRecipe &recipe = content->stonecuttingRecipes()[index];
            m_recipes.append(recipe.name);
            m_results.append(stackMap(recipe.resultItem, recipe.resultCount));
        }
    }
    emit changed();
}
