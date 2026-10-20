#include "common/content/registry/QcVillageCatalog.h"

#include <algorithm>

void QcVillageCatalog::registerStyle(QcVillageStyleDefinition style)
{
    const auto latest = std::ranges::max_element(m_styles.items(), {}, &QcVillageStyleDefinition::registered);
    style.registered = latest == m_styles.items().end() ? 1 : latest->registered + 1;
    m_styles.set(std::move(style));
}

QcVillageGolemSettings QcVillageCatalog::golemOfStyle(const QString &style) const
{
    const QcVillageStyleDefinition *definition = this->style(style);
    return definition && definition->golem ? definition->golem->over(m_settings.golem) : m_settings.golem;
}
