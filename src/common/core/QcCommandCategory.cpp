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
    const auto found = std::ranges::find_if(all(), [word](QcCommandCategory category) { return word.compare(id(category), Qt::CaseInsensitive) == 0; });
    return found != all().end() ? std::optional(*found) : std::nullopt;
}

std::optional<QcCommandCategory> fromWire(quint8 value)
{
    return value < count ? std::optional(static_cast<QcCommandCategory>(value)) : std::nullopt;
}

} // namespace QcCommandCategories
