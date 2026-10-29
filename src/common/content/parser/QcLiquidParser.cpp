// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/parser/QcLiquidParser.h"

#include "common/content/parser/QcJsonFieldReader.h"

#include <QJsonArray>

using namespace Qt::StringLiterals;

namespace {

[[nodiscard]] QList<QcLiquidReaction> parseLiquidReactions(const QJsonObject &flowJson, QcJsonFieldReader &flowReader,
                                                           const QString &packNamespace)
{
    const QJsonValue reactions = flowJson.value(u"reactions"_s);
    if (!reactions.isUndefined() && (!reactions.isArray() || reactions.toArray().size() > 16)) {
        flowReader.fail(u"'reactions' must be an array of at most 16 entries"_s);
    }

    QList<QcLiquidReaction> result;
    const QJsonArray reactionList = reactions.toArray();
    for (const auto &entry : reactionList) {
        const QJsonObject reactionJson = entry.toObject();
        QcJsonFieldReader reactionReader{reactionJson, flowReader.context, {}};
        const QcLiquidReaction reaction{reactionReader.name(u"with"_s, packNamespace), reactionReader.name(u"source"_s, packNamespace),
                                        reactionReader.name(u"flowing"_s, packNamespace)};
        if (!entry.isObject() || !reactionReader.ok() || reaction.with.isEmpty() || reaction.source.isEmpty()
            || reaction.flowing.isEmpty()) {
            flowReader.fail(reactionReader.ok() ? u"reactions need 'with', 'source' and 'flowing'"_s : reactionReader.error);
            break;
        }
        result.append(reaction);
    }
    return result;
}

} // namespace

namespace QcLiquidParser {

QcLiquidDefinition parseFlow(const QJsonObject &flowJson, QcJsonFieldReader &flowReader, const QString &packNamespace)
{
    QcLiquidDefinition liquid;
    liquid.distance = flowReader.integer(u"distance"_s, 7, 1, QcLiquidDefinition::maxDistance);
    liquid.ticks = flowReader.integer(u"ticks"_s, 5, 1, QcLiquidDefinition::maxTicks);
    liquid.infinite = flowReader.boolean(u"infinite"_s, false);
    liquid.dropSearch = flowReader.integer(u"dropSearch"_s, 4, 0, QcLiquidDefinition::maxDropSearch);
    liquid.movementFactor = flowReader.number(u"movement"_s, 1.0, 0.05, 2.0);
    liquid.bucket = flowReader.name(u"bucket"_s, packNamespace);
    liquid.bottle = flowReader.name(u"bottle"_s, packNamespace);
    liquid.sound = flowReader.name(u"sound"_s, packNamespace);
    liquid.reactions = parseLiquidReactions(flowJson, flowReader, packNamespace);
    return liquid;
}

void checkStates(const QcBlockDefinition &block, QcJsonFieldReader &flowReader)
{
    const QcLiquidDefinition &liquid = block.flow;
    const QcBlockProperty *level = block.property(u"level"_s);
    bool levelsOk = level && level->values.size() >= liquid.distance + 1;
    for (int i = 0; levelsOk && i < level->values.size(); ++i) {
        levelsOk = level->values[i] == QString::number(i);
    }

    if (flowReader.ok() && !levelsOk) {
        flowReader.fail(u"a flowing liquid needs the state 'level' with the values \"0\" to \"%1\" in order"_s.arg(liquid.distance));
    }

    const QcBlockProperty *falling = block.property(u"falling"_s);
    if (flowReader.ok() && falling && falling->values != QStringList{u"false"_s, u"true"_s}) {
        flowReader.fail(u"'falling' must have the values false and true"_s);
    }
}

} // namespace QcLiquidParser
