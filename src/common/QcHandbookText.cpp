#include "QcHandbookText.h"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <utility>

using namespace Qt::StringLiterals;

QcHandbookText::QcHandbookText(const QcContentRegistry &content, const QString &language,
                               const QMap<QString, double> &rules, const QMap<QString, QStringList> &keys)
    : m_content(content)
    , m_language(language)
    , m_rules(rules)
    , m_keys(keys)
    , m_locale(language)
{
    m_locale.setNumberOptions(QLocale::OmitGroupSeparator);
    addContentValues();
}

QString QcHandbookText::english()
{
    return u"en"_s;
}

QString QcHandbookText::lookup(const QString &key, const QString &language) const
{
    if (m_content.hasTranslation(key, language) || m_content.hasTranslation(key, english())) {
        return m_content.translate(key, language);
    }
    return {};
}

QString QcHandbookText::label(const QString &key) const
{
    return m_content.translate(u"handbook."_s + key, m_language);
}

QString QcHandbookText::number(double value) const
{
    const double rounded = std::round(value * 100.0) / 100.0;
    if (rounded == std::floor(rounded) && std::abs(rounded) < 1e15) {
        return m_locale.toString(qint64(rounded));
    }

    const int decimals = std::round(rounded * 10.0) / 10.0 == rounded ? 1 : 2;
    return m_locale.toString(rounded, 'f', decimals);
}

QString QcHandbookText::seconds(double value) const
{
    return label(u"unit.seconds"_s).arg(number(value));
}

QString QcHandbookText::yesNo(bool value) const
{
    return label(value ? u"value.yes"_s : u"value.no"_s);
}

QString QcHandbookText::percent(double share) const
{
    return label(u"unit.percent"_s).arg(number(share * 100.0));
}

QString QcHandbookText::healthPoints(double value) const
{
    return value == 1.0 ? label(u"unit.healthOne"_s) : label(u"unit.health"_s).arg(number(value));
}

QString QcHandbookText::foodPoints(int value) const
{
    return value == 1 ? label(u"unit.foodOne"_s) : label(u"unit.food"_s).arg(value);
}

QString QcHandbookText::heightRange(int minY, int maxY) const
{
    return label(u"unit.heightRange"_s).arg(minY).arg(maxY);
}

QString QcHandbookText::machineName(const QString &category) const
{
    const QString name = lookup(u"handbook.machine."_s + category);
    return name.isEmpty() ? category : name;
}

QString QcHandbookText::toolName(const QString &tool) const
{
    const QString name = lookup(u"handbook.tool."_s + tool);
    return name.isEmpty() ? tool : name;
}

QString QcHandbookText::dropNote(const QcItemDrop &drop) const
{
    QStringList parts;
    if (drop.count != 1) {
        parts.append(label(u"unit.count"_s).arg(drop.count));
    }
    if (drop.chance < 1.0) {
        parts.append(percent(drop.chance));
    }
    return parts.join(u", "_s);
}

std::optional<double> QcHandbookText::rule(const QString &name) const
{
    const auto value = m_rules.constFind(name);
    return value == m_rules.constEnd() ? std::nullopt : std::optional<double>(*value);
}

QString QcHandbookText::description(const QString &key, const QString &context)
{
    const QString text = lookup(key);
    return text.isEmpty() ? text : substituteRules(text, context);
}

QStringList QcHandbookText::takeProblems()
{
    return std::exchange(m_problems, {});
}

void QcHandbookText::addContentValues()
{
    for (const QcBlockDefinition &block : m_content.blocks()) {
        m_rules.insert(block.name + u".toolLevel"_s, block.toolLevel);
        m_rules.insert(block.name + u".slots"_s, block.containerSlots);
        m_rules.insert(block.name + u".flowDistance"_s, block.flow.distance);
        m_rules.insert(block.name + u".light"_s, block.lightLevel);
        m_rules.insert(block.name + u".hardness"_s, block.hardness);
    }

    // Items of blocks share the block's name; the block's values win.
    for (const QcItemDefinition &item : m_content.items()) {
        if (m_content.blockId(item.name)) {
            continue;
        }
        m_rules.insert(item.name + u".toolLevel"_s, item.toolLevel);
        m_rules.insert(item.name + u".food"_s, item.food);
        m_rules.insert(item.name + u".fuelSeconds"_s, item.fuelSeconds);
        m_rules.insert(item.name + u".durability"_s, item.durability);
    }

    for (const QcMobDefinition &mob : m_content.mobs()) {
        m_rules.insert(mob.name + u".health"_s, mob.maxHealth);
    }
}

QString QcHandbookText::keyText(const QStringList &keys) const
{
    if (keys.isEmpty()) {
        return label(u"key.none"_s);
    }

    QStringList names;
    for (const QString &key : keys) {
        const QString name = lookup(u"handbook.key."_s + key);
        names.append(name.isEmpty() ? key : name);
    }
    return names.join(u" / "_s);
}

QString QcHandbookText::substituteRules(const QString &text, const QString &context)
{
    static const QRegularExpression placeholder(u"\\{([A-Za-z0-9_.:-]+)\\}"_s);
    QString result;
    QStringList unknown;
    qsizetype last = 0;

    for (QRegularExpressionMatchIterator it = placeholder.globalMatch(text); it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        result += QStringView(text).mid(last, match.capturedStart() - last);
        const auto value = m_rules.constFind(match.captured(1));
        if (const auto keys = m_keys.constFind(match.captured(1)); keys != m_keys.constEnd()) {
            result += keyText(*keys);
        } else if (value == m_rules.constEnd()) {
            if (!unknown.contains(match.captured(1))) {
                unknown.append(match.captured(1));
            }
            result += match.captured(0);
        } else {
            result += number(*value);
        }
        last = match.capturedEnd();
    }

    result += QStringView(text).mid(last);
    reportUnknownRules(unknown, context);
    return result;
}

void QcHandbookText::reportUnknownRules(const QStringList &unknown, const QString &context)
{
    // One report per text, naming at most this many distinct unknown names.
    constexpr qsizetype maxNamedUnknown = 8;
    if (unknown.size() == 1) {
        m_problems.append(u"%1: unknown rule {%2}"_s.arg(context, unknown.first()));
    } else if (!unknown.isEmpty()) {
        QStringList named;
        for (qsizetype i = 0; i < std::min(unknown.size(), maxNamedUnknown); ++i) {
            named.append(u'{' + unknown[i] + u'}');
        }

        QString problem = u"%1: unknown rules %2"_s.arg(context, named.join(u", "_s));
        if (unknown.size() > maxNamedUnknown) {
            problem += u" and %1 more"_s.arg(unknown.size() - maxNamedUnknown);
        }
        m_problems.append(problem);
    }
}
