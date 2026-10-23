#include "common/gameplay/QcAdventureRules.h"

#include "common/content/definitions/QcBlockDefinition.h"
#include "common/content/definitions/QcItemDefinition.h"
#include "common/protocol/QcStreamLimits.h"

#include <QJsonArray>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

constexpr std::array actions{QcAdventureRules::Action::Break, QcAdventureRules::Action::PlaceOn};

// The strings of a JSON list; nullopt when it is no array or holds anything else.
std::optional<QStringList> stringsOf(const QJsonValue &list)
{
    if (!list.isArray()) {
        return std::nullopt;
    }

    QStringList strings;
    const QJsonArray array = list.toArray();
    for (const auto &value : array) {
        if (!value.isString()) {
            return std::nullopt;
        }
        strings.append(value.toString());
    }
    return strings;
}

} // namespace

bool QcAdventureRules::permits(QcGameMode mode, Action action, const QcItemDefinition *held, const QcBlockDefinition &block) const
{
    if (!QcGameModes::rules(mode).buildByAllowList) {
        return true;
    }

    // A bottle takes from a liquid without changing it.
    if (action == Action::PlaceOn && held && held->bottles) {
        return true;
    }

    const Lists &listed = lists(action);
    if (held && listed.value(held->name).contains(block.name)) {
        return true;
    }
    return listed.value(QString::fromLatin1(anyItem)).contains(block.name);
}

QStringList QcAdventureRules::blocks(Action action, const QString &item) const
{
    return lists(action).value(item);
}

QStringList QcAdventureRules::items(Action action) const
{
    return lists(action).keys();
}

bool QcAdventureRules::isValidName(const QString &name)
{
    return !name.isEmpty() && name.size() <= maxNameLength;
}

int QcAdventureRules::nameCount() const
{
    qsizetype count = 0;
    for (const Lists &listed : m_lists) {
        for (auto it = listed.cbegin(); it != listed.cend(); ++it) {
            count += 1 + it.value().size();
        }
    }
    return static_cast<int>(count);
}

bool QcAdventureRules::isEmpty() const
{
    return std::ranges::all_of(m_lists, [](const Lists &listed) { return listed.isEmpty(); });
}

bool QcAdventureRules::allow(Action action, const QString &item, const QStringList &blocks)
{
    Lists &listed = lists(action);
    const bool known = listed.contains(item);
    if (!isValidName(item) || (!known && listed.size() >= maxItems) || !std::ranges::all_of(blocks, isValidName)) {
        return false;
    }

    const QStringList before = listed.value(item);
    QStringList merged = before + blocks;
    merged.sort();
    merged.removeDuplicates();
    const qsizetype added = (known || merged.isEmpty() ? 0 : 1) + merged.size() - before.size();
    if (merged.size() > maxBlocks || nameCount() + added > maxNames) {
        return false;
    }

    if (!merged.isEmpty()) {
        listed.insert(item, merged);
    }
    return true;
}

bool QcAdventureRules::remove(Action action, const QString &item, const QStringList &blocks)
{
    Lists &listed = lists(action);
    const auto found = listed.find(item);
    if (found == listed.end()) {
        return false;
    }

    if (blocks.isEmpty()) {
        listed.erase(found);
        return true;
    }

    const qsizetype before = found->size();
    found->removeIf([&blocks](const QString &block) { return blocks.contains(block); });
    const bool removed = found->size() != before;
    if (found->isEmpty()) {
        listed.erase(found);
    }
    return removed;
}

void QcAdventureRules::clear()
{
    for (Lists &listed : m_lists) {
        listed.clear();
    }
}

void QcAdventureRules::merge(const QcAdventureRules &other)
{
    for (const Action action : actions) {
        for (auto it = other.lists(action).cbegin(); it != other.lists(action).cend(); ++it) {
            // A list beyond the limits is left out as a whole.
            (void)allow(action, it.key(), it.value());
        }
    }
}

QLatin1StringView QcAdventureRules::name(Action action)
{
    return action == Action::Break ? "canBreak"_L1 : "canPlaceOn"_L1;
}

std::optional<QcAdventureRules::Action> QcAdventureRules::parseAction(QStringView text)
{
    for (const Action action : actions) {
        if (text.compare(name(action), Qt::CaseInsensitive) == 0) {
            return action;
        }
    }
    return std::nullopt;
}

QJsonObject QcAdventureRules::toJson() const
{
    QJsonObject json;
    for (const Action action : actions) {
        QJsonObject items;
        for (auto it = lists(action).cbegin(); it != lists(action).cend(); ++it) {
            items.insert(it.key(), QJsonArray::fromStringList(it.value()));
        }
        if (!items.isEmpty()) {
            json.insert(name(action), items);
        }
    }
    return json;
}

QcAdventureRules::Read QcAdventureRules::fromJson(const QJsonObject &json)
{
    Read read;
    for (const Action action : actions) {
        const QJsonValue value = json.value(name(action));
        if (!value.isUndefined() && !value.isObject()) {
            read.problems.append(u"'%1' is no object; left out"_s.arg(name(action)));
            continue;
        }

        const QJsonObject items = value.toObject();
        for (auto it = items.constBegin(); it != items.constEnd(); ++it) {
            const std::optional<QStringList> blocks = stringsOf(it.value());
            if (!blocks) {
                read.problems.append(u"'%1' of '%2' is no list of block names; left out"_s.arg(it.key(), name(action)));
            } else if (!read.rules.allow(action, it.key(), *blocks)) {
                read.problems.append(u"'%1' of '%2' passes a limit (names of 1 to %3 characters, %4 items, %5 blocks each, %6 names in all); left out"_s
                                         .arg(it.key(), name(action))
                                         .arg(maxNameLength)
                                         .arg(maxItems)
                                         .arg(maxBlocks)
                                         .arg(maxNames));
            }
        }
    }
    return read;
}

void QcAdventureRules::write(QDataStream &stream) const
{
    for (const Lists &listed : m_lists) {
        stream << static_cast<quint32>(listed.size());
        for (auto it = listed.cbegin(); it != listed.cend(); ++it) {
            stream << it.key() << static_cast<quint32>(it.value().size());
            for (const QString &block : it.value()) {
                stream << block;
            }
        }
    }
}

std::optional<QcAdventureRules> QcAdventureRules::read(QDataStream &stream)
{
    QcAdventureRules rules;
    for (const Action action : actions) {
        const std::optional<quint32> itemCount = QcStreamLimits::readCount(stream, maxItems);
        for (quint32 i = 0; itemCount && i < *itemCount; ++i) {
            QString item;
            stream >> item;
            const std::optional<quint32> blockCount = QcStreamLimits::readCount(stream, maxBlocks);
            QStringList blocks;
            for (quint32 b = 0; blockCount && b < *blockCount; ++b) {
                stream >> blocks.emplace_back();
            }
            if (!blockCount || stream.status() != QDataStream::Ok || !rules.allow(action, item, blocks)) {
                return std::nullopt;
            }
        }
        if (!itemCount) {
            return std::nullopt;
        }
    }
    return rules;
}
