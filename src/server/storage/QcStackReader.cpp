#include "server/storage/QcStackReader.h"

#include "server/storage/QcComponentFrames.h"
#include "server/storage/QcItemDataCodec.h"

#include <algorithm>

using namespace Qt::StringLiterals;

QcStackReader::QcStackReader(const QcContentRegistry &content, QString path, QcLoadWarnings &warnings)
    : m_content(content)
    , m_path(std::move(path))
    , m_warnings(warnings)
{
}

QcItemStack QcStackReader::read(QDataStream &stream)
{
    return readAt(stream, QcItemData::maxDepth);
}

QcItemStack QcStackReader::readAt(QDataStream &stream, int depth)
{
    QString name;
    qint32 savedCount = 0;
    qint32 damage = 0;
    stream >> name >> savedCount >> damage;
    const bool withData = (savedCount & QcItemDataCodec::dataFlag) != 0;
    const qint32 itemCount = savedCount & ~QcItemDataCodec::dataFlag;
    std::optional<QcItemData> data = withData ? readData(stream, depth) : QcItemData{};
    if (!data) {
        stream.setStatus(QDataStream::ReadCorruptData);
        return {};
    }
    if (name.isEmpty() || itemCount <= 0) {
        return {};
    }

    const std::optional<QcItemId> item = m_content.itemId(name);
    if (!item) {
        m_warnings.addOnce(u"item:"_s + name, u"%1: unknown item '%2' dropped (in this world reported only here)"_s.arg(m_path, name));
        return {};
    }
    return fitted({*item, itemCount, damage, std::move(*data)});
}

QcItemStack QcStackReader::fitted(const Saved &saved) const
{
    const QcItemDefinition &definition = m_content.item(saved.item);
    QcItemData data = saved.data;
    // Saved before the map id was item data (decision E-823): one read moves it over.
    if (definition.map == QcMapItem::Filled && data.mapId == 0 && saved.damage > 0) {
        data.mapId = static_cast<quint32>(saved.damage);
    }
    data.name.truncate(QcItemData::maxNameLength);
    const QcItemStack plain{saved.item, 1, definition.keptDamage(saved.damage)};
    const QcItemStack stack = plain.withData(data);
    return stack.withCount(std::clamp(saved.count, 1, stack.stackLimit(definition.maxStack)));
}

std::optional<QcItemData> QcStackReader::readData(QDataStream &stream, int depth)
{
    QcItemData data;
    const auto mapId = [&data](QDataStream &in, quint16) {
        in >> data.mapId;
        return true;
    };
    const auto contents = [this, &data, depth](QDataStream &in, quint16) {
        quint16 count = 0;
        in >> count;
        if (depth <= 0 || count > QcItemData::maxContents) {
            return false;
        }
        for (quint16 i = 0; i < count && in.status() == QDataStream::Ok; ++i) {
            data.contents.append(readAt(in, depth - 1));
        }
        // Each stack inside kept the limit itself, so a level reads at most maxContents of them.
        return data.stacksInside() <= QcItemData::maxStacksInside;
    };
    const auto name = [&data](QDataStream &in, quint16) {
        const std::optional<QString> text = QcComponentFrames::readText(in, QcItemDataCodec::maxSavedName);
        data.name = text.value_or(QString());
        return text.has_value();
    };
    // Enchantments (decision E-864): names the content does not know stay on the item; they do nothing.
    const auto enchantments = [&data](QDataStream &in, quint16) {
        quint16 count = 0;
        in >> count;
        if (count > QcItemData::maxEnchantments) {
            return false;
        }
        for (quint16 i = 0; i < count && in.status() == QDataStream::Ok; ++i) {
            const std::optional<QString> name = QcComponentFrames::readText(in, QcItemData::maxEnchantmentName);
            quint16 level = 0;
            in >> level;
            if (!name || name->isEmpty() || level == 0) {
                return false;
            }
            data.enchantments.append({*name, std::min(static_cast<int>(level), QcItemData::maxEnchantmentLevel)});
        }
        return true;
    };
    // Anvil uses (decision E-866), capped like everything the anvil writes.
    const auto priorWork = [&data](QDataStream &in, quint16) {
        quint16 uses = 0;
        in >> uses;
        data.priorWork = std::min(static_cast<int>(uses), QcItemData::maxPriorWork);
        return true;
    };
    const quint16 version = QcItemDataCodec::version;
    std::optional<QcComponentFrames::ReadComponents> components =
        QcComponentFrames::read(stream, {{static_cast<quint16>(QcItemData::Component::MapId), version, mapId},
                                         {static_cast<quint16>(QcItemData::Component::Contents), version, contents},
                                         {static_cast<quint16>(QcItemData::Component::Name), version, name},
                                         {static_cast<quint16>(QcItemData::Component::Enchantments), version, enchantments},
                                         {static_cast<quint16>(QcItemData::Component::PriorWork), version, priorWork}});
    if (!components) {
        return std::nullopt;
    }
    if (components->dropped > 0) {
        const QString text = u"%1: an item stack with more than %2 unknown components; %3 of them dropped"_s;
        m_warnings.add(text.arg(m_path).arg(QcComponentFrames::maxKept).arg(components->dropped));
    }
    data.kept = std::move(components->kept);
    return data;
}
