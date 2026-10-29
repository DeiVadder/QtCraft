// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/parser/QcRailParser.h"

#include "common/content/parser/QcJsonFieldReader.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace QcRailParser {

QcRailAction parseAction(const QcBlockDefinition &block, QcJsonFieldReader &reader)
{
    const QString name = reader.string(u"railAction"_s, u"none"_s);
    QcRailAction action = QcRailAction::None;
    if (name == u"boost"_s) {
        action = QcRailAction::Boost;
    } else if (name == u"activate"_s) {
        action = QcRailAction::Activate;
    } else if (name != u"none"_s) {
        reader.fail(u"'railAction' must be none, boost or activate"_s);
    }
    if (action != QcRailAction::None && !block.rail) {
        reader.fail(u"only a rail has a 'railAction'"_s);
    }
    return action;
}

void checkStates(const QcBlockDefinition &block, QcJsonFieldReader &reader)
{
    const QcBlockProperty *facing = block.property(u"facing"_s);
    const QcBlockProperty *shape = block.property(u"shape"_s);
    const QStringList sides{u"north"_s, u"east"_s, u"south"_s, u"west"_s};
    const QStringList shapes{u"straight"_s, u"ascending"_s};
    const bool facingOk = facing && std::ranges::all_of(sides, [facing](const QString &side) { return facing->values.contains(side); });
    const bool shapeOk = shape && std::ranges::all_of(shapes, [shape](const QString &value) { return shape->values.contains(value); });
    if (!facingOk || !shapeOk) {
        reader.fail(u"a rail needs the states 'facing' (north, east, south, west) and 'shape' (straight, ascending and optionally curve)"_s);
    }
    if (block.solid || block.liquid) {
        reader.fail(u"a rail can be neither solid nor a liquid"_s);
    }
    const QcBlockProperty *powered = block.property(u"powered"_s);
    const bool poweredOk = powered && powered->values.contains(u"true"_s) && powered->values.contains(u"false"_s);
    if (block.railAction != QcRailAction::None && !poweredOk) {
        reader.fail(u"a rail with 'railAction' needs the state 'powered' (true, false)"_s);
    }
    // Power runs along a straight line of rails (QcRails::lineOf); a ring of curves shorter than the
    // line it reads would be cut at an arbitrary place.
    if (block.railAction != QcRailAction::None && shape && shape->values.contains(u"curve"_s)) {
        reader.fail(u"a rail with 'railAction' cannot have the shape 'curve'"_s);
    }
}

} // namespace QcRailParser
