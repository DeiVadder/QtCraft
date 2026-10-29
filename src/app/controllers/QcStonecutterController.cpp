// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/controllers/QcStonecutterController.h"

#include "app/controllers/QcSettingsController.h"
#include "app/debug/QcAppLogging.h"
#include "app/models/QcItemIconUrls.h"
#include "app/session/QcSessionParts.h"
#include "clientcore/session/QcClientSession.h"
#include "common/content/recipes/QcStonecutting.h"
#include "common/content/registry/QcContentRegistry.h"

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

void QcStonecutterController::select(int index) const
{
    if (!isOpen() || index < 0 || index >= m_recipes.size()) {
        qCWarning(lcGame) << "Stonecutter: no result" << index << "of" << m_recipes.size();
        return;
    }
    m_parts.session->stonecutter().select(m_recipes[index]);
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
    m_recipes.clear();
    m_results.clear();
    m_selected = -1;
    const std::shared_ptr<const QcContentRegistry> content = isOpen() ? m_parts.session->content() : nullptr;
    const QcItemStack input = content ? m_parts.session->stonecutter().input() : QcItemStack();
    if (content && !input.isEmpty() && content->isValidItem(input.item)) {
        const QList<qsizetype> recipes = QcStonecutting::recipesFor(*content, content->item(input.item).name);
        for (const qsizetype index : recipes) {
            const QcSmeltingRecipe &recipe = content->stonecuttingRecipes()[index];
            m_recipes.append(recipe.name);
            m_results.append(stackMap(recipe.resultItem, recipe.resultCount));
        }
        m_selected = static_cast<int>(m_recipes.indexOf(m_parts.session->stonecutter().recipe()));
    }
    emit changed();
}
