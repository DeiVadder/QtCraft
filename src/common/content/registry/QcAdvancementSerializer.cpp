#include "common/content/registry/QcAdvancementSerializer.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/protocol/QcStreamLimits.h"

using namespace Qt::StringLiterals;

namespace {

constexpr qsizetype maxKeyLength = 256;
// The parser allows criterion names of 1 to 64 characters.
constexpr qsizetype maxCriterionLength = 64;

bool plausible(const QcAdvancementDefinition &advancement, quint8 frame)
{
    const bool shortNames = !advancement.name.isEmpty() && advancement.name.size() <= QcAdvancementDefinition::maxNameLength
                            && advancement.parent.size() <= QcAdvancementDefinition::maxNameLength
                            && advancement.icon.size() <= QcAdvancementDefinition::maxNameLength
                            && advancement.handbook.size() <= maxKeyLength && advancement.titleKey.size() <= maxKeyLength
                            && advancement.descriptionKey.size() <= maxKeyLength;
    return shortNames && frame <= static_cast<quint8>(QcAdvancementFrame::Challenge);
}

// The requirement groups, each a list of criterion names; std::nullopt beyond the bounds.
std::optional<QList<QStringList>> readRequirements(QDataStream &stream)
{
    const std::optional<quint32> count = QcStreamLimits::readCount(stream, QcAdvancementDefinition::maxCriteria);
    if (!count) {
        return std::nullopt;
    }
    QList<QStringList> groups;
    for (quint32 i = 0; i < *count; ++i) {
        std::optional<QStringList> group = QcStreamLimits::readBoundedStrings(stream, QcAdvancementDefinition::maxCriteria, maxCriterionLength);
        if (!group) {
            return std::nullopt;
        }
        groups.append(std::move(*group));
    }
    return groups;
}

} // namespace

namespace QcAdvancementSerializer {

void write(QDataStream &stream, const QcAdvancementCatalog &advancements)
{
    stream << static_cast<quint32>(advancements.items().size());
    for (const QcAdvancementDefinition &advancement : advancements.items()) {
        QStringList criteria;
        for (const QcAdvancementCriterion &criterion : advancement.criteria) {
            criteria.append(criterion.name);
        }
        stream << advancement.name << advancement.parent << advancement.icon << advancement.titleKey << advancement.descriptionKey
               << advancement.handbook << static_cast<quint8>(advancement.frame) << advancement.hidden << criteria
               << static_cast<quint32>(advancement.requirements.size());
        for (const QStringList &group : advancement.requirements) {
            stream << group;
        }
    }
}

QcStatus read(QDataStream &stream, QcContentRegistry &registry)
{
    const std::optional<quint32> count = QcStreamLimits::readCount(stream, QcAdvancementCatalog::maxAdvancements);
    if (!count) {
        return qcError(u"Corrupt content: advancement table"_s);
    }
    for (quint32 i = 0; i < *count; ++i) {
        QcAdvancementDefinition advancement;
        quint8 frame = 0;
        stream >> advancement.name >> advancement.parent >> advancement.icon >> advancement.titleKey >> advancement.descriptionKey
            >> advancement.handbook >> frame >> advancement.hidden;
        const std::optional<QStringList> criteria = QcStreamLimits::readBoundedStrings(stream, QcAdvancementDefinition::maxCriteria, maxCriterionLength);
        std::optional<QList<QStringList>> requirements = criteria ? readRequirements(stream) : std::nullopt;
        if (stream.status() != QDataStream::Ok || !requirements || !plausible(advancement, frame)) {
            return qcError(u"Corrupt content: advancement %1"_s.arg(i));
        }
        advancement.frame = static_cast<QcAdvancementFrame>(frame);
        for (const QString &name : *criteria) {
            advancement.criteria.append(QcAdvancementCriterion{name, QcAdvancementTrigger::Impossible, {}});
        }
        advancement.requirements = std::move(*requirements);
        registry.advancements().add(std::move(advancement));
    }
    return {};
}

} // namespace QcAdvancementSerializer
