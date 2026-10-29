// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/villages/QcVillageCatalog.h"

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
