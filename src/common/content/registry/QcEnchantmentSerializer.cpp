#include "common/content/registry/QcEnchantmentSerializer.h"

#include "common/content/parser/QcEnchantmentParser.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/protocol/QcStreamLimits.h"

using namespace Qt::StringLiterals;

namespace {

// At most this many names in `items` or `conflicts`, as the parser allows.
constexpr qsizetype maxNames = 64;

bool plausible(const QcEnchantmentDefinition &enchantment, quint8 effect)
{
    const auto allKinds = [](const QStringList &kinds) { return std::ranges::all_of(kinds, QcEnchantmentParser::isItemKind); };
    return !enchantment.name.isEmpty() && effect <= static_cast<quint8>(QcEnchantmentEffect::FeatherFalling) && enchantment.maxLevel >= 1
           && enchantment.maxLevel <= QcEnchantmentDefinition::maxLevelLimit && enchantment.weight >= 1 && enchantment.weight <= 1000
           && enchantment.items.size() <= maxNames && enchantment.conflicts.size() <= maxNames && allKinds(enchantment.items)
           && enchantment.minPower >= 1 && enchantment.minPower <= 100 && enchantment.powerPerLevel >= 0 && enchantment.powerPerLevel <= 100
           && enchantment.powerSpread >= 0 && enchantment.powerSpread <= 100;
}

} // namespace

namespace QcEnchantmentSerializer {

void write(QDataStream &stream, const QList<QcEnchantmentDefinition> &enchantments)
{
    stream << static_cast<quint32>(enchantments.size());
    for (const QcEnchantmentDefinition &enchantment : enchantments) {
        stream << enchantment.name << enchantment.translationKey << static_cast<quint8>(enchantment.effect) << static_cast<qint32>(enchantment.maxLevel)
               << static_cast<qint32>(enchantment.weight) << enchantment.items << enchantment.conflicts << static_cast<qint32>(enchantment.minPower)
               << static_cast<qint32>(enchantment.powerPerLevel) << static_cast<qint32>(enchantment.powerSpread);
    }
}

QcStatus read(QDataStream &stream, QcContentRegistry &registry)
{
    const std::optional<quint32> count = QcStreamLimits::readCount(stream, QcStreamLimits::maxDefinitions);
    if (!count) {
        return qcError(u"Corrupt content: enchantment table"_s);
    }
    for (quint32 i = 0; i < *count; ++i) {
        QcEnchantmentDefinition enchantment;
        quint8 effect = 0;
        qint32 maxLevel = 0;
        qint32 weight = 0;
        qint32 minPower = 0;
        qint32 powerPerLevel = 0;
        qint32 powerSpread = 0;
        stream >> enchantment.name >> enchantment.translationKey >> effect >> maxLevel >> weight >> enchantment.items
            >> enchantment.conflicts >> minPower >> powerPerLevel >> powerSpread;
        enchantment.effect = static_cast<QcEnchantmentEffect>(effect);
        enchantment.maxLevel = maxLevel;
        enchantment.weight = weight;
        enchantment.minPower = minPower;
        enchantment.powerPerLevel = powerPerLevel;
        enchantment.powerSpread = powerSpread;
        if (stream.status() != QDataStream::Ok || !plausible(enchantment, effect)) {
            return qcError(u"Corrupt content: enchantment %1"_s.arg(i));
        }
        registry.registerEnchantment(std::move(enchantment));
    }
    return {};
}

} // namespace QcEnchantmentSerializer
