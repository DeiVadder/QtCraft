#include "common/content/definitions/QcHoldDisplay.h"

#include <QStringList>

using namespace Qt::StringLiterals;

namespace {

// In the order of the enums.
const QStringList &categoryNames()
{
    static const QStringList names{u"tool"_s, u"weapon"_s, u"launcher"_s, u"sprite"_s, u"block"_s, u"partialBlock"_s};
    return names;
}

const QStringList &gripNames()
{
    static const QStringList names{u"base"_s, u"handle"_s, u"middle"_s};
    return names;
}

template<typename Value>
std::optional<Value> either(const std::optional<Value> &mine, const std::optional<Value> &base)
{
    return mine ? mine : base;
}

} // namespace

QcHoldTransform QcHoldTransform::over(const QcHoldTransform &base) const
{
    return {either(translate, base.translate), either(rotate, base.rotate), either(scale, base.scale)};
}

QcHoldDisplay QcHoldDisplay::over(const QcHoldDisplay &base) const
{
    return {either(category, base.category), either(grip, base.grip), firstPerson.over(base.firstPerson), thirdPerson.over(base.thirdPerson),
            swing.over(base.swing)};
}

QcHoldDefaults QcHoldDefaults::merged(const QcHoldDefaults &later) const
{
    QcHoldDefaults result;
    for (std::size_t i = 0; i < categories.size(); ++i) {
        result.categories[i] = later.categories[i].over(categories[i]);
    }
    return result;
}

namespace QcHoldNames {

QString category(QcHoldCategory category)
{
    return categoryNames().value(static_cast<int>(category));
}

std::optional<QcHoldCategory> category(const QString &name)
{
    const qsizetype index = categoryNames().indexOf(name);
    return index < 0 ? std::nullopt : std::optional(static_cast<QcHoldCategory>(index));
}

QString grip(QcHoldGrip grip)
{
    return gripNames().value(static_cast<int>(grip));
}

std::optional<QcHoldGrip> grip(const QString &name)
{
    const qsizetype index = gripNames().indexOf(name);
    return index < 0 ? std::nullopt : std::optional(static_cast<QcHoldGrip>(index));
}

} // namespace QcHoldNames
