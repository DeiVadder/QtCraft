// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/core/QcCommandCategory.h"

#include <algorithm>

using Category = QcCommandCategory;
using namespace Qt::StringLiterals;

namespace QcCommandCategories {

const std::array<QcCommandCategory, count> &all()
{
    static constexpr std::array<QcCommandCategory, count> categories{Category::General,  Category::Players, Category::Items,
                                                                     Category::Creatures, Category::Building, Category::World,
                                                                     Category::Finding,   Category::Server,   Category::Other};
    return categories;
}

QLatin1StringView id(QcCommandCategory category)
{
    static constexpr std::array<QLatin1StringView, count> ids{"general"_L1,  "players"_L1, "items"_L1,  "creatures"_L1, "building"_L1,
                                                              "world"_L1,    "finding"_L1, "server"_L1, "other"_L1};
    return ids[static_cast<std::size_t>(category)];
}

std::optional<QcCommandCategory> fromId(QStringView word)
{
    // QLatin1StringView::compare(QStringView) is defined in <QLatin1StringView>; QStringView::compare(QLatin1StringView)
    // is only declared there and defined inline in <QString>, which left an undefined symbol in static release builds.
    const auto found = std::ranges::find_if(all(), [word](QcCommandCategory category) { return id(category).compare(word, Qt::CaseInsensitive) == 0; });
    return found != all().end() ? std::optional(*found) : std::nullopt;
}

std::optional<QcCommandCategory> fromWire(quint8 value)
{
    return value < count ? std::optional(static_cast<QcCommandCategory>(value)) : std::nullopt;
}

} // namespace QcCommandCategories
