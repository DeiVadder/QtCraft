// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/worldgen/QcProgressionGoalParser.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/content/registry/QcJsonFieldReader.h"

#include <array>

using namespace Qt::StringLiterals;

namespace {

using Goal = QcProgressionGoal;

// A key that says what a goal asks for, the kind it stands for and whether its value is a name
// (else it can only be true).
struct KindKey
{
    QString key;
    Goal::Kind kind;
    bool named;
};

const std::array<KindKey, 5> &kindKeys()
{
    static const std::array<KindKey, 5> keys{KindKey{u"item"_s, Goal::Kind::Item, true}, KindKey{u"food"_s, Goal::Kind::Food, false},
                                             KindKey{u"fire"_s, Goal::Kind::Fire, false}, KindKey{u"portal"_s, Goal::Kind::Portal, true},
                                             KindKey{u"dimension"_s, Goal::Kind::Dimension, true}};
    return keys;
}

// The one key of `json` that says what the goal asks for; none when it has none or several.
const KindKey *onlyKindKey(const QJsonObject &json)
{
    const KindKey *found = nullptr;
    for (const KindKey &key : kindKeys()) {
        if (json.contains(key.key)) {
            if (found) {
                return nullptr;
            }
            found = &key;
        }
    }
    return found;
}

// Whether the target of `goal` exists in `content`.
bool targetExists(const Goal &goal, const QcContentRegistry &content)
{
    switch (goal.kind) {
    case Goal::Kind::Item:
        return content.itemId(goal.target).has_value();
    case Goal::Kind::Portal:
        return content.worldgen().portal(goal.target) != nullptr;
    case Goal::Kind::Dimension:
        return goal.target == QString::fromLatin1(QcDimensionDefinition::overworldName) || content.worldgen().dimension(goal.target) != nullptr;
    case Goal::Kind::Food:
    case Goal::Kind::Fire:
        break;
    }
    return true;
}

} // namespace

namespace QcProgressionGoalParser {

QcResult<QcProgressionGoal> parseProgressionGoal(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"progression goal"_s, {}};
    Goal goal;
    goal.name = reader.name(u"name"_s, packNamespace);
    if (goal.name.isEmpty()) {
        return qcError(reader.ok() ? u"progression goal: 'name' is required"_s : reader.error);
    }

    reader.context = u"progression goal '%1'"_s.arg(goal.name);
    const KindKey *key = onlyKindKey(json);
    if (!key) {
        reader.fail(u"exactly one of 'item', 'food', 'fire', 'portal' and 'dimension' is required"_s);
        return qcError(reader.error);
    }

    goal.kind = key->kind;
    if (key->named) {
        goal.target = reader.name(key->key, packNamespace);
        if (goal.target.isEmpty() && reader.ok()) {
            reader.fail(u"'%1' must name something"_s.arg(key->key));
        }
    } else if (!reader.boolean(key->key, false)) {
        reader.fail(u"'%1' can only be true"_s.arg(key->key));
    }
    if (json.contains(u"count"_s) && goal.kind != Goal::Kind::Item) {
        reader.fail(u"'count' goes only with 'item'"_s);
    }
    goal.count = reader.integer(u"count"_s, goal.count, 1, Goal::maxCount);
    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return goal;
}

QStringList checkTargets(const QcContentRegistry &content)
{
    QStringList warnings;
    for (const Goal &goal : content.worldgen().progressionGoals()) {
        if (!targetExists(goal, content)) {
            warnings.append(u"progression goal '%1': '%2' does not exist; it can never be reached"_s.arg(goal.name, goal.target));
        }
    }
    return warnings;
}

} // namespace QcProgressionGoalParser
