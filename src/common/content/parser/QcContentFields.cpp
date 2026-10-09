#include "QcContentFields.h"

#include "QcContentNames.h"
#include "QcJsonFieldReader.h"

#include <QJsonArray>

using namespace Qt::StringLiterals;

namespace {

constexpr qsizetype maxCategoryLength = 64;

} // namespace

namespace QcContentFields {

bool isValidCategory(const QString &category)
{
    return category.size() <= maxCategoryLength && !category.contains(u'/') && QcContentNames::isValidPart(category);
}

QString invalidCategoryMessage()
{
    return u"'category' must be a name of lower-case letters, digits, '_', '-' and '.' (at most 64 characters)"_s;
}

QString localPart(const QString &qualifiedName)
{
    return qualifiedName.mid(qualifiedName.indexOf(u':') + 1);
}

QString translationKey(const QString &prefix, const QString &qualifiedName)
{
    return u"%1.%2.%3"_s.arg(prefix, qualifiedName.left(qualifiedName.indexOf(u':')), localPart(qualifiedName).replace(u'/', u'.'));
}

QString qualifyIngredient(const QString &text, const QString &packNamespace)
{
    if (text.startsWith(u'#')) {
        const QString tag = QcContentNames::qualify(text.mid(1), packNamespace);
        return tag.isEmpty() ? QString() : u'#' + tag;
    }
    return QcContentNames::qualify(text, packNamespace);
}

QStringList parseTags(const QJsonValue &value, const QString &packNamespace, QcJsonFieldReader &reader)
{
    QStringList tags;
    if (value.isUndefined()) {
        return tags;
    }

    const QJsonArray list = value.toArray();
    if (!value.isArray() || list.size() > 16) {
        reader.fail(u"'tags' must be an array of at most 16 names"_s);
        return tags;
    }

    for (const auto &entry : list) {
        const QString tag = entry.isString() ? QcContentNames::qualify(entry.toString(), packNamespace) : QString();
        if (tag.isEmpty()) {
            reader.fail(u"invalid tag"_s);
            return {};
        }
        tags.append(tag);
    }
    return tags;
}

std::optional<QString> stateValue(const QJsonValue &value)
{
    if (value.isString()) {
        return value.toString();
    }
    if (value.isBool()) {
        return value.toBool() ? u"true"_s : u"false"_s;
    }
    return std::nullopt;
}

} // namespace QcContentFields
