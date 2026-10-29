// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/structures/QcStateRotation.h"

#include "worldgen/generators/QcWorldgenLogging.h"

#include <QHash>
#include <QMutex>
#include <QSet>
#include <QtLogging>

#include <array>

using namespace Qt::StringLiterals;

namespace {

// Clockwise order seen from above.
const std::array<QString, 4> &directions()
{
    static const std::array<QString, 4> names{u"north"_s, u"east"_s, u"south"_s, u"west"_s};
    return names;
}

int directionIndex(const QString &name)
{
    for (int i = 0; i < 4; ++i) {
        if (directions()[static_cast<std::size_t>(i)] == name) {
            return i;
        }
    }
    return -1;
}

// Once per block, property and value: structures are placed chunk by chunk on several threads.
void warnMissingValue(const QString &block, const QString &property, const QString &value)
{
    static QMutex mutex;
    static QSet<QString> reported;
    const QString key = block + u'|' + property + u'=' + value;
    {
        const QMutexLocker locker(&mutex);
        if (reported.contains(key)) {
            return;
        }
        reported.insert(key);
    }
    qCWarning(lcWorldgen, "Turning %s: it has no state %s=%s; that property keeps its value", qPrintable(block), qPrintable(property), qPrintable(value));
}

} // namespace

namespace QcStateRotation {

QString turnedDirection(const QString &direction, int turns)
{
    const int index = directionIndex(direction);
    if (index < 0) {
        return direction;
    }
    return directions()[static_cast<std::size_t>(((index + turns) % 4 + 4) % 4)];
}

QcBlockId turned(const QcContentRegistry &content, QcBlockId state, int turns)
{
    turns = ((turns % 4) + 4) % 4;
    if (turns == 0 || !content.isValidBlock(state)) {
        return state;
    }

    // All new values first: the side properties swap among each other.
    const QcBlockDefinition &block = content.block(state);
    QHash<QString, QString> values;
    for (const QcBlockProperty &property : block.properties) {
        const QString value = content.property(state, property.name);
        const QString name = turnedDirection(property.name, turns);
        if (property.name == u"axis"_s && turns % 2 == 1) {
            values.insert(name, value == u"x"_s ? u"z"_s : (value == u"z"_s ? u"x"_s : value));
        } else if (property.name == u"rotation"_s && property.values.size() == 16) {
            // A standing sign's sixteenths of a turn (decision E-766).
            values.insert(name, QString::number((value.toInt() + 4 * turns) % 16));
        } else {
            values.insert(name, turnedDirection(value, turns));
        }
    }

    QcBlockId result = state;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        result = content.withProperty(result, it.key(), it.value());
        if (content.property(result, it.key()) != it.value()) {
            warnMissingValue(block.name, it.key(), it.value());
        }
    }
    return result;
}

} // namespace QcStateRotation
